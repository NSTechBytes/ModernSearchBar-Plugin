#include <Windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include "../API/sqlite3.h"
#include <codecvt>
#include <locale>
#include <wininet.h>
#include <sstream>
#include "../API/RainmeterAPI.h"
#include <set>
#pragma comment(lib, "wininet.lib")

std::wstring Utf8ToWide(const std::string& utf8Str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(utf8Str);
}
//=====================================================================================================================================================//
//                                                        Copy Chrome File                                                                             //
//=====================================================================================================================================================//

std::wstring CopyChromeHistoryToTemp() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);

    std::wstring tempFolder = tempPath;
    std::wstring chromeHistorySource = L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\History";
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
//=====================================================================================================================================================//
//                                                        Chrome History                                                                               //
//=====================================================================================================================================================//

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
//=====================================================================================================================================================//
//                                                         Top Trends                                                                                  //
//=====================================================================================================================================================//

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

//=====================================================================================================================================================//
//                                                         Rainmeter Function                                                                          //
//=====================================================================================================================================================//

struct Measure {
    std::wstring type;
    int num;
    std::wstring onCompleteAction;
    std::vector<std::wstring> historyTitles;
    void* skin;

    Measure() : type(L""), num(0), onCompleteAction(L""), skin(nullptr) {}
};

PLUGIN_EXPORT void Initialize(void** data, void* rm) {
    Measure* measure = new Measure;
    *data = measure;

    measure->type = RmReadString(rm, L"Type", L"");
    measure->num = static_cast<int>(RmReadInt(rm, L"Num", 5));
    measure->skin = RmGetSkin(rm);

    if (measure->type == L"Chrome_History") {
        std::wstring dbPath = CopyChromeHistoryToTemp();
        if (!dbPath.empty()) {
            measure->historyTitles = GetLastHistoryTitles(dbPath, measure->num);
        }
        else {
            RmLog(rm, LOG_ERROR, L"Could not copy Chrome history database.");
        }
    }
    else if (measure->type == L"Top_Trends") {
        std::wstring trendsUrl = L"https://trends.google.com/trends/trendingsearches/daily/rss?geo=US";
        measure->historyTitles = GetTopTrends(trendsUrl, measure->num);
    }
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue) {
    Measure* measure = (Measure*)data;

    measure->type = RmReadString(rm, L"Type", L"");
    measure->num = static_cast<int>(RmReadInt(rm, L"Num", 5));
    measure->onCompleteAction = RmReadString(rm, L"OnCompleteAction", L"", FALSE);

    if (measure->type == L"Chrome_History") {
        std::wstring dbPath = CopyChromeHistoryToTemp();
        if (!dbPath.empty()) {
            measure->historyTitles = GetLastHistoryTitles(dbPath, measure->num);
        }
    }
    else if (measure->type == L"Top_Trends") {
        std::wstring trendsUrl = L"https://trends.google.com/trends/trendingsearches/daily/rss?geo=US";
        measure->historyTitles = GetTopTrends(trendsUrl, measure->num);
    }
}

PLUGIN_EXPORT double Update(void* data) {
    return 0.0;
}

PLUGIN_EXPORT LPCWSTR GetString(void* data) {
    Measure* measure = (Measure*)data;

    static std::wstring result;
    result.clear();

    if (!measure->historyTitles.empty()) {
        for (size_t i = 0; i < measure->historyTitles.size(); ++i) {
            result += measure->historyTitles[i];
            if (i < measure->historyTitles.size() - 1) {
                result += L" | ";
            }
        }


        if (!measure->onCompleteAction.empty()) {
            RmExecute(measure->skin, measure->onCompleteAction.c_str());
        }
    }
    else {
        result = L"No data found.";
    }

    return result.c_str();
}

PLUGIN_EXPORT void Finalize(void* data) {
    Measure* measure = (Measure*)data;
    delete measure;
}
