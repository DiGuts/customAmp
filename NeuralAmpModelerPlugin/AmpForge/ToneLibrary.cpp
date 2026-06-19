#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "ToneLibrary.h"
#include "../iPlug2/Dependencies/Extras/nlohmann/json.hpp"

#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

// URL encode helper
static std::string UrlEncode(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Parse https://host/path into host and path wstrings
static void ParseUrl(const std::string& fullUrl, std::wstring& host, INTERNET_PORT& port, std::wstring& path)
{
    // strip scheme
    std::string url = fullUrl;
    bool isHttps = true;
    if (url.substr(0,8) == "https://") url = url.substr(8);
    else if (url.substr(0,7) == "http://") { url = url.substr(7); isHttps = false; }

    port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    auto slash = url.find('/');
    std::string hostStr = (slash != std::string::npos) ? url.substr(0, slash) : url;
    std::string pathStr = (slash != std::string::npos) ? url.substr(slash) : "/";

    // check for port in host
    auto colon = hostStr.find(':');
    if (colon != std::string::npos)
    {
        port = (INTERNET_PORT)std::stoi(hostStr.substr(colon+1));
        hostStr = hostStr.substr(0, colon);
    }

    host = std::wstring(hostStr.begin(), hostStr.end());
    path = std::wstring(pathStr.begin(), pathStr.end());
}

std::string ToneLibrary::HttpGet(const std::string& fullUrl)
{
    std::wstring host, path;
    INTERNET_PORT port;
    ParseUrl(fullUrl, host, port, path);

    bool isHttps = (port == INTERNET_DEFAULT_HTTPS_PORT || fullUrl.substr(0,8) == "https://");

    HINTERNET hSession = WinHttpOpen(L"AmpForge/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return {};
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
    {
        std::vector<char> buf(avail);
        DWORD read = 0;
        WinHttpReadData(hReq, buf.data(), avail, &read);
        body.append(buf.data(), read);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

bool ToneLibrary::HttpSaveToFile(const std::string& fullUrl, const std::string& destPath)
{
    std::wstring host, path;
    INTERNET_PORT port;
    ParseUrl(fullUrl, host, port, path);

    bool isHttps = (port == INTERNET_DEFAULT_HTTPS_PORT || fullUrl.substr(0,8) == "https://");

    HINTERNET hSession = WinHttpOpen(L"AmpForge/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream ofs(destPath, std::ios::binary);
    if (!ofs) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
    {
        std::vector<char> buf(avail);
        DWORD read = 0;
        WinHttpReadData(hReq, buf.data(), avail, &read);
        ofs.write(buf.data(), read);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

std::string ToneLibrary::BuildUrl(const std::string& endpoint, const std::string& params) const
{
    std::string url = "https://www.tone3000.com/api/v1/" + endpoint + "?api_key=" + mApiKey;
    if (!params.empty()) url += "&" + params;
    return url;
}

void ToneLibrary::SearchAsync(const std::string& query, int page, OnSearch cb)
{
    std::string q = query;
    std::string apiKey = mApiKey;
    std::thread([q, page, cb, apiKey, this]() {
        std::string params = "q=" + UrlEncode(q) + "&page=" + std::to_string(page) + "&page_size=8";
        std::string url = BuildUrl("tones/search", params);
        std::string body = HttpGet(url);
        if (body.empty()) { cb({}, page, 1, "Network error"); return; }
        try {
            auto j = json::parse(body);
            std::vector<ToneItem> items;
            for (auto& jt : j.value("data", json::array()))
            {
                ToneItem t;
                t.id = jt.value("id", 0);
                t.name = jt.value("name", "");
                auto author = jt.value("author", json::object());
                if (author.is_object())
                    t.authorName = author.value("name", "");
                t.modelCount = jt.value("model_count", 0);
                items.push_back(t);
            }
            int totalPages = 1;
            if (j.contains("pagination") && j["pagination"].is_object())
                totalPages = j["pagination"].value("total_pages", 1);
            cb(items, page, totalPages, "");
        } catch (...) {
            cb({}, page, 1, "Parse error");
        }
    }).detach();
}

void ToneLibrary::GetModelsAsync(int toneId, OnModels cb)
{
    std::thread([toneId, cb, this]() {
        std::string params = "tone_id=" + std::to_string(toneId);
        std::string url = BuildUrl("models", params);
        std::string body = HttpGet(url);
        if (body.empty()) { cb({}, "Network error"); return; }
        try {
            auto j = json::parse(body);
            std::vector<ModelItem> items;
            for (auto& jm : j.value("data", json::array()))
            {
                ModelItem m;
                m.id = jm.value("id", 0);
                m.toneId = toneId;
                m.name = jm.value("name", "");
                m.modelUrl = jm.value("model_url", "");
                m.size = jm.value("size", "");
                items.push_back(m);
            }
            cb(items, "");
        } catch (...) {
            cb({}, "Parse error");
        }
    }).detach();
}

void ToneLibrary::DownloadAsync(const ModelItem& model, const std::string& destDir, OnDownload cb)
{
    ModelItem m = model;
    std::thread([m, destDir, cb, this]() {
        // Build safe filename
        std::string safeName = m.name;
        for (char& c : safeName)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        std::string destPath = destDir + "\\" + safeName + ".nam";

        std::string url = m.modelUrl;
        if (url.find('?') == std::string::npos)
            url += "?api_key=" + mApiKey;
        else
            url += "&api_key=" + mApiKey;

        bool ok = HttpSaveToFile(url, destPath);
        if (ok) cb(destPath, "");
        else    cb("", "Download failed");
    }).detach();
}

#endif // _WIN32
