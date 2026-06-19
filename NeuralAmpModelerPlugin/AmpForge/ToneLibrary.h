#pragma once
#ifdef _WIN32

#include <string>
#include <vector>
#include <functional>
#include <mutex>

struct ToneItem   { int id=0; std::string name; std::string authorName; int modelCount=0; };
struct ModelItem  { int id=0; int toneId=0; std::string name; std::string modelUrl; std::string size; };

class ToneLibrary
{
public:
    using OnSearch   = std::function<void(std::vector<ToneItem>, int page, int totalPages, std::string err)>;
    using OnModels   = std::function<void(std::vector<ModelItem>, std::string err)>;
    using OnDownload = std::function<void(std::string filePath, std::string err)>;

    void SetApiKey(const std::string& k) { mApiKey = k; }
    const std::string& GetApiKey() const { return mApiKey; }

    void SearchAsync(const std::string& query, int page, OnSearch cb);
    void GetModelsAsync(int toneId, OnModels cb);
    void DownloadAsync(const ModelItem& model, const std::string& destDir, OnDownload cb);

private:
    std::string mApiKey;
    static std::string HttpGet(const std::string& fullUrl);
    static bool        HttpSaveToFile(const std::string& fullUrl, const std::string& destPath);
    std::string BuildUrl(const std::string& endpoint, const std::string& params = {}) const;
};
#endif
