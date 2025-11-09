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
#include <algorithm>
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

std::vector<std::wstring> GetLastHistoryTitles(const std::wstring& dbPath) {
    std::vector<std::wstring> historyTitles;
    std::set<std::wstring> seenTitles;

    sqlite3* db = nullptr;
    sqlite3_stmt* stmt = nullptr;
    std::string query = "SELECT title FROM urls ORDER BY last_visit_time DESC";

    if (sqlite3_open16(dbPath.c_str(), &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
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

std::vector<std::wstring> GetTopTrends(const std::wstring& url) {
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
            while ((itemPos = rssContent.find("<title>", itemPos)) != std::string::npos) {
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
* Rainmeter API Functions - Parent/Child Pattern
*/

struct ChildMeasure;

struct ParentMeasure {
    void* skin;
    LPCWSTR name;
    ChildMeasure* ownerChild;

    std::wstring type;
    std::wstring countryCode;
    std::wstring profile;
    std::wstring onCompleteAction;
    std::vector<std::wstring> results;
    
    std::thread workerThread;
    std::mutex dataMutex;
    std::atomic<bool> isLoading;
    std::atomic<bool> dataReady;
    bool hasExecutedAction;

    ParentMeasure() : skin(nullptr), name(nullptr), ownerChild(nullptr),
                      type(L""), countryCode(L"US"), profile(L"Default"), 
                      onCompleteAction(L""), isLoading(false), 
                      dataReady(false), hasExecutedAction(false) {}
    
    ~ParentMeasure() {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
};

struct ChildMeasure {
    int index;
    ParentMeasure* parent;

    ChildMeasure() : index(1), parent(nullptr) {}
};

std::vector<ParentMeasure*> g_ParentMeasures;

void LoadDataAsync(ParentMeasure* parent, void* rm) {
    parent->isLoading = true;
    std::vector<std::wstring> tempResults;

    if (parent->type == L"Chrome_History") {
        std::wstring dbPath = CopyChromeHistoryToTemp(parent->profile);
        if (!dbPath.empty()) {
            tempResults = GetLastHistoryTitles(dbPath);
        }
        else {
            if (rm) RmLog(rm, LOG_ERROR, L"Could not copy Chrome history database.");
        }
    }
    else if (parent->type == L"Top_Trends") {
        std::wstring trendsUrl = L"https://trends.google.com/trending/rss?geo=" + parent->countryCode;
        tempResults = GetTopTrends(trendsUrl);
    }

    // Thread-safe update
    {
        std::lock_guard<std::mutex> lock(parent->dataMutex);
        parent->results = tempResults;
        parent->dataReady = true;
    }
    
    parent->isLoading = false;
}

PLUGIN_EXPORT void Initialize(void** data, void* rm) {
    ChildMeasure* child = new ChildMeasure;
    *data = child;

    void* skin = RmGetSkin(rm);

    LPCWSTR parentName = RmReadString(rm, L"ParentName", L"");
    if (!*parentName) {
        // This is a parent measure
        child->parent = new ParentMeasure;
        child->parent->name = RmGetMeasureName(rm);
        child->parent->skin = skin;
        child->parent->ownerChild = child;
        g_ParentMeasures.push_back(child->parent);

        child->parent->type = RmReadString(rm, L"Type", L"");
        child->parent->countryCode = RmReadString(rm, L"CountryCode", L"US");
        child->parent->profile = RmReadString(rm, L"Profile", L"Default");
        child->parent->onCompleteAction = RmReadString(rm, L"OnCompleteAction", L"", FALSE);

        // Start async loading
        if (!child->parent->type.empty()) {
            child->parent->workerThread = std::thread(LoadDataAsync, child->parent, rm);
        }
    }
    else {
        // This is a child measure - find parent using name AND skin handle
        std::vector<ParentMeasure*>::const_iterator iter = g_ParentMeasures.begin();
        for (; iter != g_ParentMeasures.end(); ++iter) {
            if (_wcsicmp((*iter)->name, parentName) == 0 && (*iter)->skin == skin) {
                child->parent = (*iter);
                return;
            }
        }

        RmLog(rm, LOG_ERROR, L"Invalid \"ParentName\"");
    }
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue) {
    ChildMeasure* child = (ChildMeasure*)data;
    ParentMeasure* parent = child->parent;

    if (!parent) {
        return;
    }

    // Read child-specific options
    child->index = static_cast<int>(RmReadInt(rm, L"Index", 1));

    // Read parent-specific options (only for owner child)
    if (parent->ownerChild == child) {
        // Wait for previous thread to finish
        if (parent->workerThread.joinable()) {
            parent->workerThread.join();
        }

        parent->type = RmReadString(rm, L"Type", L"");
        parent->countryCode = RmReadString(rm, L"CountryCode", L"US");
        parent->profile = RmReadString(rm, L"Profile", L"Default");
        parent->onCompleteAction = RmReadString(rm, L"OnCompleteAction", L"", FALSE);

        // Reset flags
        parent->dataReady = false;
        parent->hasExecutedAction = false;

        // Start async loading
        if (!parent->type.empty()) {
            parent->workerThread = std::thread(LoadDataAsync, parent, rm);
        }
    }
}

PLUGIN_EXPORT double Update(void* data) {
    ChildMeasure* child = (ChildMeasure*)data;
    ParentMeasure* parent = child->parent;
    
    if (!parent) {
        return 0.0;
    }
    
    // Check if data is ready and execute action once (only for owner child)
    if (parent->ownerChild == child) {
        if (parent->dataReady && !parent->hasExecutedAction) {
            if (!parent->onCompleteAction.empty()) {
                RmExecute(parent->skin, parent->onCompleteAction.c_str());
                parent->hasExecutedAction = true;
            }
        }
    }
    
    return parent->isLoading ? 1.0 : 0.0;
}

PLUGIN_EXPORT LPCWSTR GetString(void* data) {
    ChildMeasure* child = (ChildMeasure*)data;
    ParentMeasure* parent = child->parent;

    static std::wstring result;
    result.clear();

    if (!parent) {
        result = L"Error: No parent measure";
        return result.c_str();
    }

    // Thread-safe read
    std::lock_guard<std::mutex> lock(parent->dataMutex);
    
    // Return the specific index (1-based)
    if (!parent->results.empty()) {
        if (child->index > 0 && child->index <= static_cast<int>(parent->results.size())) {
            result = parent->results[child->index - 1];
        }
        else {
            result = L"";
        }
    }
    else if (parent->isLoading) {
        result = L"Loading...";
    }
    else if (parent->dataReady) {
        result = L"No data found.";
    }
    else {
        result = L"Initializing...";
    }

    return result.c_str();
}

PLUGIN_EXPORT void Finalize(void* data) {
    ChildMeasure* child = (ChildMeasure*)data;
    ParentMeasure* parent = child->parent;

    if (parent && parent->ownerChild == child) {
        // Wait for worker thread to complete before cleanup
        if (parent->workerThread.joinable()) {
            parent->workerThread.join();
        }

        g_ParentMeasures.erase(
            std::remove(g_ParentMeasures.begin(), g_ParentMeasures.end(), parent),
            g_ParentMeasures.end());
        delete parent;
    }

    delete child;
}
