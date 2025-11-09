#include <Windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include "../sqlite3/sqlite3.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <wininet.h>
#include <sstream>
#include "../API/RainmeterAPI.h"
#include <set>
#pragma comment(lib, "wininet.lib")

std::wstring Utf8ToWide(const std::string& utf8Str) {
    if (utf8Str.empty()) {
        return std::wstring();
    }

    int wideStrLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wideStrLen == 0) {
        return std::wstring();
    }

    std::wstring wideStr(wideStrLen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wideStr[0], wideStrLen);
    return wideStr;
}

/*
* Copy Chrome DB File
*/

std::wstring CopyChromeHistoryToTemp(const std::wstring& profile) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);

    std::wstring tempFolder = tempPath;
    std::wstring chromeHistorySource = L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\" + profile + L"\\History";
    std::wstring chromeHistoryTarget = tempFolder + L"History_Copy.db";

    wchar_t resolvedPath[MAX_PATH];
    ExpandEnvironmentStringsW(chromeHistorySource.c_str(), resolvedPath, MAX_PATH);

    if (std::filesystem::exists(resolvedPath)) {
        try {
            std::filesystem::copy(resolvedPath, chromeHistoryTarget, std::filesystem::copy_options::overwrite_existing);
            return chromeHistoryTarget;
        }
        catch (const std::exception& e) {
            OutputDebugStringA(e.what());
        }
    }
    else {
        OutputDebugStringW(L"Chrome History file not found.");
    }

    return L"";
}

/*
* Parse Chrome History
*/

std::vector<std::wstring> GetLastHistoryTitles(const std::wstring& dbPath, int num) {
    std::vector<std::wstring> historyTitles;
    std::set<std::wstring> seenTitles;

    sqlite3* db = nullptr;
    sqlite3_stmt* stmt = nullptr;
    std::string query = "SELECT title FROM urls ORDER BY last_visit_time DESC";

    if (sqlite3_open16(dbPath.c_str(), &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(historyTitles.size()) < num) {
                const unsigned char* title = sqlite3_column_text(stmt, 0);
                if (title) {
                    std::string utf8Title(reinterpret_cast<const char*>(title));
                    std::wstring wideTitle = Utf8ToWide(utf8Title);

                    if (seenTitles.find(wideTitle) == seenTitles.end()) {
                        historyTitles.push_back(wideTitle);
                        seenTitles.insert(wideTitle);
                    }
                }
                else {
                    std::wstring noTitle = L"(No Title)";
                    if (seenTitles.find(noTitle) == seenTitles.end()) {
                        historyTitles.push_back(noTitle);
                        seenTitles.insert(noTitle);
                    }
                }
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }

    return historyTitles;
}

/*
*  Fetch Top Searches
*/

std::vector<std::wstring> GetTopTrends(const std::wstring& url, int num) {
    std::vector<std::wstring> trends;

    HINTERNET hInternet = InternetOpenW(L"RainmeterPlugin", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (hInternet) {
        HINTERNET hConnect = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD, 0);
        if (hConnect) {
            char buffer[1024];
            DWORD bytesRead;
            std::stringstream rssStream;

            while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                rssStream.write(buffer, bytesRead);
            }
            InternetCloseHandle(hConnect);

            std::string rssContent = rssStream.str();
            size_t itemPos = 0;
            while ((itemPos = rssContent.find("<title>", itemPos)) != std::string::npos && trends.size() < static_cast<size_t>(num)) {
                size_t start = itemPos + 7;
                size_t end = rssContent.find("</title>", start);
                if (end != std::string::npos) {
                    std::string utf8Title = rssContent.substr(start, end - start);


                    if (utf8Title != "Daily Search Trends") {
                        trends.push_back(Utf8ToWide(utf8Title));
                    }

                    itemPos = end;
                }
                else {
                    break;
                }
            }
        }
        InternetCloseHandle(hInternet);
    }

    return trends;
}

/*
* Rainmeter API Functions
*/

struct Measure {
    std::wstring type;
    int num;
    std::wstring countryCode;
    std::wstring profile;
    std::wstring onCompleteAction;
    std::vector<std::wstring> historyTitles;
    void* skin;
    
    std::thread workerThread;
    std::mutex dataMutex;
    std::atomic<bool> isLoading;
    std::atomic<bool> dataReady;
    bool hasExecutedAction;

    Measure() : type(L""), num(0), countryCode(L"US"), profile(L"Default"), 
                onCompleteAction(L""), skin(nullptr), isLoading(false), 
                dataReady(false), hasExecutedAction(false) {}
    
    ~Measure() {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
};

void LoadDataAsync(Measure* measure, void* rm) {
    measure->isLoading = true;
    std::vector<std::wstring> tempResults;

    if (measure->type == L"Chrome_History") {
        std::wstring dbPath = CopyChromeHistoryToTemp(measure->profile);
        if (!dbPath.empty()) {
            tempResults = GetLastHistoryTitles(dbPath, measure->num);
        }
        else {
            if (rm) RmLog(rm, LOG_ERROR, L"Could not copy Chrome history database.");
        }
    }
    else if (measure->type == L"Top_Trends") {
        std::wstring trendsUrl = L"https://trends.google.com/trending/rss?geo=" + measure->countryCode;
        tempResults = GetTopTrends(trendsUrl, measure->num);
    }

    // Thread-safe update
    {
        std::lock_guard<std::mutex> lock(measure->dataMutex);
        measure->historyTitles = tempResults;
        measure->dataReady = true;
    }
    
    measure->isLoading = false;
}

PLUGIN_EXPORT void Initialize(void** data, void* rm) {
    Measure* measure = new Measure;
    *data = measure;

    measure->type = RmReadString(rm, L"Type", L"");
    measure->num = static_cast<int>(RmReadInt(rm, L"Num", 5));
    measure->countryCode = RmReadString(rm, L"CountryCode", L"US");
    measure->profile = RmReadString(rm, L"Profile", L"Default");
    measure->onCompleteAction = RmReadString(rm, L"OnCompleteAction", L"", FALSE);
    measure->skin = RmGetSkin(rm);

    // Start async loading
    if (!measure->type.empty()) {
        measure->workerThread = std::thread(LoadDataAsync, measure, rm);
    }
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue) {
    Measure* measure = (Measure*)data;

    // Wait for previous thread to finish
    if (measure->workerThread.joinable()) {
        measure->workerThread.join();
    }

    measure->type = RmReadString(rm, L"Type", L"");
    measure->num = static_cast<int>(RmReadInt(rm, L"Num", 5));
    measure->countryCode = RmReadString(rm, L"CountryCode", L"US");
    measure->profile = RmReadString(rm, L"Profile", L"Default");
    measure->onCompleteAction = RmReadString(rm, L"OnCompleteAction", L"", FALSE);

    // Reset flags
    measure->dataReady = false;
    measure->hasExecutedAction = false;

    // Start async loading
    if (!measure->type.empty()) {
        measure->workerThread = std::thread(LoadDataAsync, measure, rm);
    }
}

PLUGIN_EXPORT double Update(void* data) {
    Measure* measure = (Measure*)data;
    
    // Check if data is ready and execute action once
    if (measure->dataReady && !measure->hasExecutedAction) {
        if (!measure->onCompleteAction.empty()) {
            RmExecute(measure->skin, measure->onCompleteAction.c_str());
            measure->hasExecutedAction = true;
        }
    }
    
    return measure->isLoading ? 1.0 : 0.0;
}

PLUGIN_EXPORT LPCWSTR GetString(void* data) {
    Measure* measure = (Measure*)data;

    static std::wstring result;
    result.clear();

    // Thread-safe read
    std::lock_guard<std::mutex> lock(measure->dataMutex);
    
    // Always show cached data if available, even while loading
    if (!measure->historyTitles.empty()) {
        for (size_t i = 0; i < measure->historyTitles.size(); ++i) {
            result += measure->historyTitles[i];
            if (i < measure->historyTitles.size() - 1) {
                result += L" | ";
            }
        }
    }
    else if (measure->isLoading) {
        result = L"Loading...";
    }
    else if (measure->dataReady) {
        result = L"No data found.";
    }
    else {
        result = L"Initializing...";
    }

    return result.c_str();
}

PLUGIN_EXPORT void Finalize(void* data) {
    Measure* measure = (Measure*)data;
    
    // Wait for worker thread to complete before cleanup
    if (measure->workerThread.joinable()) {
        measure->workerThread.join();
    }
    
    delete measure;
}
