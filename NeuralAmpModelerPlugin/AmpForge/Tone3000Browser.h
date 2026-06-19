#pragma once
#ifdef _WIN32

#include <shlobj.h>

#include "IControls.h"
#include "ToneLibrary.h"
#include "Colors.h"
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <filesystem>

using namespace iplug;
using namespace igraphics;

class Tone3000BrowserControl : public IControl
{
public:
    enum class State { Empty, Busy, Tones, Models, Downloading, Done };

    struct PendingResult {
        enum Type { Tones, Models, Download, Error } type;
        std::vector<ToneItem> tones;
        std::vector<ModelItem> models;
        int page=1, totalPages=1;
        std::string downloadPath, err;
    };

    Tone3000BrowserControl(const IRECT& bounds, ToneLibrary& lib,
                            std::function<void(const std::string&)> onModelLoaded,
                            const IVStyle& style)
    : IControl(bounds)
    , mLib(lib)
    , mOnModelLoaded(onModelLoaded)
    , mStyle(style)
    {}

    void Draw(IGraphics& g) override
    {
        const IColor bgColor    = PluginColors::HOTONE_BG;
        const IColor panelColor = PluginColors::HOTONE_PANEL;
        const IColor btnColor   = PluginColors::HOTONE_BTN;
        const IColor borderColor= PluginColors::HOTONE_BORDER;
        const IColor amberColor = PluginColors::HOTONE_AMBER;
        const IColor dimColor   = PluginColors::HOTONE_DIM;
        const IColor greyColor  = PluginColors::HOTONE_GREY;
        const IColor white      = IColor(255,255,255,255);

        // Background overlay
        g.FillRect(IColor(230,10,10,10), mRECT);

        // Main panel
        IRECT panel = mRECT.GetPadded(-20.f);
        g.FillRect(panelColor, panel);
        g.DrawRect(borderColor, panel);

        // Title
        IRECT titleRect = panel.GetFromTop(36.f);
        g.FillRect(bgColor, titleRect);
        IText titleText(18.f, amberColor);
        g.DrawText(titleText, "TONE3000 BROWSER", titleRect);

        // Close button
        mCloseRect = titleRect.GetFromRight(36.f);
        g.FillRect(btnColor, mCloseRect);
        IText closeText(16.f, white);
        g.DrawText(closeText, "X", mCloseRect);

        float y = panel.T + 40.f;

        // Search bar
        mSearchBarRect = IRECT(panel.L + 8.f, y, panel.R - 90.f, y + 28.f);
        g.FillRect(bgColor, mSearchBarRect);
        g.DrawRect(borderColor, mSearchBarRect);
        std::string searchDisplay = mSearchText.empty() ? "Search tones..." : mSearchText;
        IColor searchTextColor = mSearchText.empty() ? dimColor : white;
        IText searchText(13.f, searchTextColor);
        g.DrawText(searchText, searchDisplay.c_str(), mSearchBarRect.GetPadded(-4.f));

        // Search button
        mSearchBtnRect = IRECT(mSearchBarRect.R + 4.f, y, panel.R - 4.f, y + 28.f);
        g.FillRect(btnColor, mSearchBtnRect);
        IText searchBtnText(13.f, white);
        g.DrawText(searchBtnText, "GO", mSearchBtnRect);

        y += 34.f;

        // Status
        IText statusText(12.f, amberColor);
        IRECT statusRect = IRECT(panel.L + 8.f, y, panel.R - 8.f, y + 20.f);
        g.DrawText(statusText, mStatus.c_str(), statusRect);
        y += 24.f;

        // Results area
        float rowH = 34.f;
        mRowRects.clear();

        if (mState == State::Tones || mState == State::Busy)
        {
            for (int i = 0; i < (int)mTones.size() && i < 8; i++)
            {
                IRECT row = IRECT(panel.L + 8.f, y, panel.R - 8.f, y + rowH - 2.f);
                mRowRects.push_back(row);
                bool selected = (mSelectedToneIdx == i);
                g.FillRect(selected ? btnColor : bgColor, row);
                g.DrawRect(borderColor, row);

                // Tone name
                IText nameText(13.f, white);
                IRECT nameRect = row.GetFromLeft(row.W() * 0.6f).GetPadded(-4.f);
                g.DrawText(nameText, mTones[i].name.c_str(), nameRect);

                // Author
                IText authorText(11.f, dimColor);
                IRECT authorRect = IRECT(row.L + row.W() * 0.6f, row.T, row.L + row.W() * 0.85f, row.B).GetPadded(-4.f);
                g.DrawText(authorText, mTones[i].authorName.c_str(), authorRect);

                // Model count chip
                IRECT chipRect = IRECT(row.R - 44.f, row.T + 6.f, row.R - 4.f, row.B - 6.f);
                g.FillRect(amberColor, chipRect);
                IText chipText(11.f, IColor(255,0,0,0));
                g.DrawText(chipText, std::to_string(mTones[i].modelCount).c_str(), chipRect);

                y += rowH;
            }
        }
        else if (mState == State::Models || mState == State::Downloading || mState == State::Done)
        {
            // Back button
            mBackRect = IRECT(panel.L + 8.f, y, panel.L + 70.f, y + 24.f);
            g.FillRect(btnColor, mBackRect);
            IText backText(12.f, white);
            g.DrawText(backText, "< BACK", mBackRect);
            y += 30.f;

            for (int i = 0; i < (int)mModels.size() && i < 8; i++)
            {
                IRECT row = IRECT(panel.L + 8.f, y, panel.R - 8.f, y + rowH - 2.f);
                mRowRects.push_back(row);
                bool selected = (mSelectedModelIdx == i);
                g.FillRect(selected ? btnColor : bgColor, row);
                g.DrawRect(borderColor, row);

                IText nameText(13.f, white);
                IRECT nameRect = row.GetFromLeft(row.W() * 0.7f).GetPadded(-4.f);
                g.DrawText(nameText, mModels[i].name.c_str(), nameRect);

                if (!mModels[i].size.empty())
                {
                    IRECT sizeRect = IRECT(row.R - 64.f, row.T + 6.f, row.R - 4.f, row.B - 6.f);
                    g.FillRect(greyColor, sizeRect);
                    IText sizeText(11.f, white);
                    g.DrawText(sizeText, mModels[i].size.c_str(), sizeRect);
                }

                y += rowH;
            }
        }
        else if (mState == State::Empty)
        {
            IText emptyText(14.f, dimColor);
            IRECT emptyRect = IRECT(panel.L, y, panel.R, y + 40.f);
            g.DrawText(emptyText, "Search for tones above", emptyRect);
        }

        // Pagination (shown in Tones state)
        if (mState == State::Tones && mTotalPages > 1)
        {
            float pyBottom = panel.B - 40.f;
            mPrevRect = IRECT(panel.L + 8.f, pyBottom, panel.L + 60.f, pyBottom + 28.f);
            mNextRect = IRECT(panel.R - 68.f, pyBottom, panel.R - 8.f, pyBottom + 28.f);

            bool hasPrev = mCurrentPage > 1;
            bool hasNext = mCurrentPage < mTotalPages;

            g.FillRect(hasPrev ? btnColor : dimColor, mPrevRect);
            IText pageText(12.f, white);
            g.DrawText(pageText, "< PREV", mPrevRect);

            g.FillRect(hasNext ? btnColor : dimColor, mNextRect);
            g.DrawText(pageText, "NEXT >", mNextRect);

            std::string pageStr = std::to_string(mCurrentPage) + " / " + std::to_string(mTotalPages);
            IRECT pageNumRect = IRECT(mPrevRect.R + 4.f, pyBottom, mNextRect.L - 4.f, pyBottom + 28.f);
            IText pageNumText(12.f, greyColor);
            g.DrawText(pageNumText, pageStr.c_str(), pageNumRect);
        }

        // Download button (shown when a model is selected)
        if ((mState == State::Models || mState == State::Done) && mSelectedModelIdx >= 0 && mSelectedModelIdx < (int)mModels.size())
        {
            float dyBottom = panel.B - 40.f;
            mDownloadRect = IRECT(panel.R - 130.f, dyBottom, panel.R - 8.f, dyBottom + 28.f);
            g.FillRect(amberColor, mDownloadRect);
            IText dlText(13.f, IColor(255,0,0,0));
            g.DrawText(dlText, "DOWNLOAD", mDownloadRect);
        }
    }

    void OnMouseDown(float x, float y, const IMouseMod& mod) override
    {
        // Close
        if (mCloseRect.Contains(x, y))
        {
            Hide(true);
            return;
        }

        // Search bar - open text entry
        if (mSearchBarRect.Contains(x, y))
        {
            GetUI()->CreateTextEntry(*this, IText(14.f, IColor(255,255,255,255)), mSearchBarRect, mSearchText.c_str());
            return;
        }

        // Search button
        if (mSearchBtnRect.Contains(x, y) && !mSearchText.empty())
        {
            DoSearch(1);
            return;
        }

        // Back button (in Models state)
        if ((mState == State::Models || mState == State::Downloading || mState == State::Done) && mBackRect.Contains(x, y))
        {
            mState = State::Tones;
            mSelectedModelIdx = -1;
            SetDirty(false);
            return;
        }

        // Row clicks
        for (int i = 0; i < (int)mRowRects.size(); i++)
        {
            if (mRowRects[i].Contains(x, y))
            {
                if (mState == State::Tones)
                {
                    mSelectedToneIdx = i;
                    if (i < (int)mTones.size())
                        LoadModels(mTones[i].id);
                }
                else if (mState == State::Models || mState == State::Done)
                {
                    mSelectedModelIdx = i;
                    SetDirty(false);
                }
                return;
            }
        }

        // Pagination
        if (mState == State::Tones)
        {
            if (mPrevRect.Contains(x, y) && mCurrentPage > 1)
            {
                DoSearch(mCurrentPage - 1);
                return;
            }
            if (mNextRect.Contains(x, y) && mCurrentPage < mTotalPages)
            {
                DoSearch(mCurrentPage + 1);
                return;
            }
        }

        // Download
        if (mDownloadRect.Contains(x, y) && mSelectedModelIdx >= 0 && mSelectedModelIdx < (int)mModels.size())
        {
            DoDownload();
        }
    }

    void OnTextEntryCompletion(const char* str, int) override
    {
        if (str && *str)
        {
            mSearchText = str;
            DoSearch(1);
        }
    }

    void Poll()
    {
        std::vector<PendingResult> results;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            results.swap(mPending);
        }
        for (auto& r : results)
        {
            if (r.type == PendingResult::Type::Error)
            {
                mStatus = "Error: " + r.err;
                if (mState == State::Busy) mState = State::Empty;
                SetDirty(false);
            }
            else if (r.type == PendingResult::Type::Tones)
            {
                mTones = r.tones;
                mCurrentPage = r.page;
                mTotalPages = r.totalPages;
                mState = State::Tones;
                mStatus = mTones.empty() ? "No results" : "Found " + std::to_string(mTones.size()) + " tones";
                SetDirty(false);
            }
            else if (r.type == PendingResult::Type::Models)
            {
                mModels = r.models;
                mState = State::Models;
                mStatus = mModels.empty() ? "No models" : std::to_string(mModels.size()) + " models available";
                SetDirty(false);
            }
            else if (r.type == PendingResult::Type::Download)
            {
                if (!r.err.empty())
                {
                    mStatus = "Error: " + r.err;
                    mState = State::Models;
                }
                else
                {
                    mStatus = "Downloaded: " + r.downloadPath;
                    mState = State::Done;
                    if (mOnModelLoaded) mOnModelLoaded(r.downloadPath);
                }
                SetDirty(false);
            }
        }
    }

private:
    void DoSearch(int page)
    {
        mState = State::Busy;
        mStatus = "Searching...";
        SetDirty(false);
        mLib.SearchAsync(mSearchText, page, [this](std::vector<ToneItem> tones, int pg, int totalPg, std::string err) {
            PendingResult r;
            if (!err.empty()) { r.type = PendingResult::Type::Error; r.err = err; }
            else { r.type = PendingResult::Type::Tones; r.tones = tones; r.page = pg; r.totalPages = totalPg; }
            PostResult(r);
        });
    }

    void LoadModels(int toneId)
    {
        mState = State::Busy;
        mStatus = "Loading models...";
        mSelectedModelIdx = -1;
        SetDirty(false);
        mLib.GetModelsAsync(toneId, [this](std::vector<ModelItem> models, std::string err) {
            PendingResult r;
            if (!err.empty()) { r.type = PendingResult::Type::Error; r.err = err; }
            else { r.type = PendingResult::Type::Models; r.models = models; }
            PostResult(r);
        });
    }

    void DoDownload()
    {
        if (mSelectedModelIdx < 0 || mSelectedModelIdx >= (int)mModels.size()) return;
        mState = State::Downloading;
        mStatus = "Downloading...";
        SetDirty(false);

        // Destination: documents folder / AmpForge
        std::string destDir;
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path)))
            destDir = std::string(path) + "\\AmpForge";
        else
            destDir = ".";

        // Create dir if needed
        std::filesystem::create_directories(destDir);

        ModelItem m = mModels[mSelectedModelIdx];
        mLib.DownloadAsync(m, destDir, [this](std::string filePath, std::string err) {
            PendingResult r;
            r.type = PendingResult::Type::Download;
            r.downloadPath = filePath;
            r.err = err;
            PostResult(r);
        });
    }

    void PostResult(const PendingResult& r)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mPending.push_back(r);
    }

    ToneLibrary& mLib;
    std::function<void(const std::string&)> mOnModelLoaded;
    IVStyle mStyle;

    State mState = State::Empty;
    std::string mSearchText;
    std::string mStatus = "Enter a search term above";

    std::vector<ToneItem> mTones;
    std::vector<ModelItem> mModels;
    int mCurrentPage = 1;
    int mTotalPages = 1;
    int mSelectedToneIdx = -1;
    int mSelectedModelIdx = -1;

    // Layout rects (computed each Draw)
    IRECT mCloseRect, mSearchBarRect, mSearchBtnRect, mBackRect;
    IRECT mPrevRect, mNextRect, mDownloadRect;
    std::vector<IRECT> mRowRects;

    std::mutex mMutex;
    std::vector<PendingResult> mPending;
};

#endif // _WIN32
