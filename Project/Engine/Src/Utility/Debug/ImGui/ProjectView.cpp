#include "ProjectView.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/Logger/Logger.h"

#include <imgui.h>
#include <algorithm>
#include <Windows.h>
#include <shellapi.h>

namespace CoreEngine
{
    void ProjectView::Initialize(DirectXCommon* dxCommon)
    {
        dxCommon_ = dxCommon;

        // ルートパスをプロジェクトルート（仮想ルート）に設定
        rootPath_ = std::filesystem::current_path();
        appAssetsPath_ = rootPath_ / "Application" / "Assets";
        engineAssetsPath_ = rootPath_ / "Engine" / "Assets";
        currentPath_ = rootPath_;

        // デフォルトアイコンを読み込み
        LoadDefaultIcon();

        currentEntries_ = GetCurrentDirectoryContents();
    }

    void ProjectView::Update()
    {
        if (!isVisible_) {
            return;
        }

        // ウィンドウを開始
        if (ImGui::Begin("Project", &isVisible_, ImGuiWindowFlags_NoCollapse)) {

            // パンくずリスト表示
            DrawBreadcrumb();

            ImGui::Separator();

            // 2カラムレイアウト（左：フォルダツリー、右：グリッドビュー）
            ImGui::BeginChild("LeftPanel", ImVec2(treeViewWidth_, 0), true);
            {
                // フォルダツリーを描画
                DrawFolderTree(rootPath_);
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // 右側のグリッドビュー
            ImGui::BeginChild("RightPanel", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            {
                currentEntries_ = GetCurrentDirectoryContents();
                DrawGridLayout(currentEntries_);
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void ProjectView::Finalize()
    {
        directoryIconTexture_.Reset();
        pngIconTexture_.Reset();
        fileIconTexture_.Reset();
        pngPreviewCache_.clear();
        pngPreviewInfoCache_.clear();
    }

    std::vector<ProjectView::Entry> ProjectView::GetCurrentDirectoryContents()
    {
        // 仮想ルート：Application/Assets と Engine/Assets のみ表示
        if (currentPath_ == rootPath_) {
            std::vector<Entry> entries;
            auto addRoot = [&](const std::filesystem::path& path, const std::string& label) {
                if (!std::filesystem::exists(path)) return;
                Entry e;
                e.name = label;
                e.path = path;
                e.isDirectory = true;
                entries.push_back(e);
            };
            addRoot(appAssetsPath_, "Application");
            addRoot(engineAssetsPath_, "Engine");
            return entries;
        }

        std::vector<Entry> entries;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentPath_)) {
                // .meta ファイルは非表示にする
                if (!entry.is_directory() && entry.path().extension() == ".meta") {
                    continue;
                }

                Entry e;
                e.name = entry.path().filename().string();
                e.path = entry.path();
                e.isDirectory = entry.is_directory();
                entries.push_back(e);
            }

            // ディレクトリを先に、次にファイルをアルファベット順にソート
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory; // ディレクトリを優先
                }
                return a.name < b.name;
                });
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}", "Failed to read directory: " + std::string(e.what()));
        }

        return entries;
    }

    void ProjectView::NavigateToDirectory(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path) || path == currentPath_) {
            return;
        }

        currentPath_ = path;
        currentEntries_ = GetCurrentDirectoryContents();
        selectedIndex_ = -1;
    }

    void ProjectView::NavigateUp()
    {
        if (currentPath_ == rootPath_) return;

        // 両アセットルートからは仮想ルートに戻る
        if (currentPath_ == appAssetsPath_ || currentPath_ == engineAssetsPath_) {
            NavigateToDirectory(rootPath_);
            return;
        }

        if (currentPath_.has_parent_path()) {
            NavigateToDirectory(currentPath_.parent_path());
        }
    }

    void ProjectView::DrawGridLayout(const std::vector<Entry>& entries)
    {
        if (entries.empty()) {
            ImGui::TextDisabled("(Empty folder)");
            return;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        float itemWidth = iconSize_ + padding_ * 2.0f;
        float itemHeight = iconSize_ + textHeight_ + padding_ * 2.0f;

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];

            ImGui::PushID(static_cast<int>(i));

            // アイコンとテキストを含むグループ
            ImGui::BeginGroup();

            // 選択状態の背景（Unity風の薄い白/グレー）
            bool isSelected = (selectedIndex_ == static_cast<int>(i));
            if (isSelected) {
                ImVec2 rectMin = ImGui::GetCursorScreenPos();
                ImVec2 rectMax = ImVec2(rectMin.x + itemWidth, rectMin.y + itemHeight);
                // Unity風の薄い白/グレー色
                ImU32 selectionColor = ImGui::GetColorU32(ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rectMin, rectMax,
                    selectionColor
                );
            }

            ImGui::Dummy(ImVec2(padding_, padding_));

            // アイコンを中央揃え
            DrawIcon(entry);

            // ファイル名（折り返し表示と省略）
            std::string displayName = entry.name;
            
            // ファイル名が長すぎる場合は省略
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + itemWidth - padding_ * 2.0f);
            
            // テキストのサイズを計算
            ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str(), nullptr, false, itemWidth - padding_ * 2.0f);
            
            // 高さが2行分を超える場合は省略
            float maxHeight = ImGui::GetTextLineHeight() * 2.0f;
            if (textSize.y > maxHeight) {
                // 文字数を徐々に減らして "..." を追加
                size_t maxLen = displayName.length();
                while (maxLen > 3) {
                    std::string truncated = displayName.substr(0, maxLen - 3) + "...";
                    textSize = ImGui::CalcTextSize(truncated.c_str(), nullptr, false, itemWidth - padding_ * 2.0f);
                    if (textSize.y <= maxHeight) {
                        displayName = truncated;
                        break;
                    }
                    maxLen--;
                }
            }
            
            // テキストを中央揃え
            textSize = ImGui::CalcTextSize(displayName.c_str(), nullptr, false, itemWidth - padding_ * 2.0f);
            float textOffsetX = (itemWidth - textSize.x) * 0.5f;
            if (textOffsetX > 0) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
            }
            
            ImGui::Text("%s", displayName.c_str());
            ImGui::PopTextWrapPos();
            
            // ホバー時にフルネームをツールチップで表示
            if (ImGui::IsItemHovered() && displayName != entry.name) {
                ImGui::SetTooltip("%s", entry.name.c_str());
            }

            ImGui::EndGroup();

            // クリック検出
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                double currentTime = ImGui::GetTime();

                // ダブルクリック判定
                if (lastClickedIndex_ == static_cast<int>(i) &&
                    (currentTime - lastClickTime_) < 0.3) {

                    // ダブルクリック: ディレクトリに移動
                    if (entry.isDirectory) {
                        NavigateToDirectory(entry.path);
                    } else {
                        OpenInExplorer(entry.path);
                    }

                    lastClickedIndex_ = -1;
                    lastClickTime_ = 0.0;
                } else {
                    // シングルクリック: 選択
                    selectedIndex_ = static_cast<int>(i);
                    lastClickedIndex_ = static_cast<int>(i);
                    lastClickTime_ = currentTime;
                }
            }

            // 次の項目を同じ行に配置（ウィンドウ幅を超える場合は改行）
            float lastItemX = ImGui::GetItemRectMax().x;
            float nextItemX = lastItemX + style.ItemSpacing.x + itemWidth;
            if (i + 1 < entries.size() && nextItemX < windowVisibleX) {
                ImGui::SameLine();
            }

            ImGui::PopID();
        }
    }

    void ProjectView::DrawIcon(const Entry& entry)
    {
        ImTextureID texID = 0;
        bool hasValidTexture = false;
        ImVec2 displaySize = ImVec2(iconSize_, iconSize_);
        
        // エントリのタイプに応じてアイコンを選択
        if (entry.isDirectory) {
            // ディレクトリアイコン
            if (directoryIconLoaded_ && directoryIconTexture_) {
                texID = (ImTextureID)directoryIconGpuHandle_.ptr;
                hasValidTexture = true;
            }
        }
        else {
            // ファイルの拡張子を取得
            std::string extension = entry.path.extension().string();
            for (auto& c : extension) {
                c = static_cast<char>(::tolower(c));
            }
            
            if (extension == ".png") {
                // PNGファイルの場合は実際の画像をプレビュー
                auto preview = GetPNGPreview(entry.path);
                if (preview.gpuHandle.ptr != 0) {
                    texID = (ImTextureID)preview.gpuHandle.ptr;
                    displaySize = ImVec2(preview.width, preview.height);
                    hasValidTexture = true;
                }
                else if (pngIconLoaded_ && pngIconTexture_) {
                    // プレビューが取得できない場合はPNGアイコン
                    texID = (ImTextureID)pngIconGpuHandle_.ptr;
                    hasValidTexture = true;
                }
            }
            else {
                // その他のファイルアイコン
                if (fileIconLoaded_ && fileIconTexture_) {
                    texID = (ImTextureID)fileIconGpuHandle_.ptr;
                    hasValidTexture = true;
                }
            }
        }
        
        if (hasValidTexture && texID) {
            // アイコンを中央揃えで描画
            float offsetX = (iconSize_ - displaySize.x) * 0.5f;
            if (offsetX > 0) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX + padding_);
            } else {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding_);
            }
            
            // テクスチャを使用してアイコンを描画
            ImGui::Image(texID, displaySize);
        }
        else {
            // テクスチャがない場合はプレースホルダー
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            cursorPos.x += padding_;
            ImVec2 size = ImVec2(iconSize_, iconSize_);

            ImU32 color = entry.isDirectory
                ? ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.3f, 1.0f))  // フォルダは黄色
                : ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // ファイルはグレー

            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + size.x, cursorPos.y + size.y),
                color
            );

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding_);
            ImGui::Dummy(size);
        }
    }

    void ProjectView::LoadDefaultIcon()
    {
        auto& textureManager = TextureManager::GetInstance();

        // ディレクトリアイコンを読み込み
        try {
            auto texture = textureManager.Load("directoryIcon.png");
            if (texture.texture) {
                directoryIconTexture_ = texture.texture;
                directoryIconGpuHandle_ = texture.gpuHandle;
                directoryIconLoaded_ = true;
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}", "Loaded directory icon for ProjectView");
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to load directory icon: " + std::string(e.what()));
            directoryIconLoaded_ = false;
        }

        // PNGアイコンを読み込み
        try {
            auto texture = textureManager.Load("pngIcon.png");
            if (texture.texture) {
                pngIconTexture_ = texture.texture;
                pngIconGpuHandle_ = texture.gpuHandle;
                pngIconLoaded_ = true;
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}", "Loaded PNG icon for ProjectView");
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to load PNG icon: " + std::string(e.what()));
            pngIconLoaded_ = false;
        }

        // ファイルアイコンを読み込み（ファイル名のみ - AssetDatabaseが自動解決）
        try {
            auto texture = textureManager.Load("fileIcon.png");
            if (texture.texture) {
                fileIconTexture_ = texture.texture;
                fileIconGpuHandle_ = texture.gpuHandle;
                fileIconLoaded_ = true;
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}", "Loaded file icon for ProjectView");
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to load file icon: " + std::string(e.what()));
            fileIconLoaded_ = false;
        }
    }

    void ProjectView::DrawFolderTree(const std::filesystem::path& path)
    {
        auto updateExpandAlpha = [this](const std::filesystem::path& nodePath) {
            std::string key = nodePath.generic_string();

            auto it = treeExpandAnimTime_.find(key);
            if (it == treeExpandAnimTime_.end()) {
                return 1.0f;
            }

            it->second += ImGui::GetIO().DeltaTime;
            float duration = (treeExpandAnimDuration_ > 0.0f) ? treeExpandAnimDuration_ : 0.01f;
            float t = std::clamp(it->second / duration, 0.0f, 1.0f);
            float eased = t * t * (3.0f - 2.0f * t);

            bool opening = true;
            auto dirIt = treeExpandAnimOpening_.find(key);
            if (dirIt != treeExpandAnimOpening_.end()) {
                opening = dirIt->second;
            }

            float alpha = opening ? eased : (1.0f - eased);
            if (t >= 1.0f) {
                treeExpandAnimTime_.erase(it);
                treeExpandAnimOpening_.erase(key);
                if (!opening) {
                    treePendingClose_.erase(key);
                    treeCloseCommit_[key] = true;
                    return 0.0f;
                }
                return 1.0f;
            }

            return alpha;
        };

        auto hasSubdirectories = [](const std::filesystem::path& dirPath) {
            for (const auto& subEntry : std::filesystem::directory_iterator(dirPath)) {
                if (subEntry.is_directory()) {
                    return true;
                }
            }
            return false;
        };

        auto isUnderPath = [](const std::filesystem::path& child, const std::filesystem::path& parent) {
            auto rel = std::filesystem::relative(child, parent);
            return !rel.empty() && rel.native()[0] != '.';
        };

        auto drawFolderNode = [&](const std::filesystem::path& nodePath, const std::string& nodeLabel, bool defaultOpen) {
            bool hasChildren = hasSubdirectories(nodePath);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (nodePath == currentPath_) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (defaultOpen) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (!hasChildren) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }

            std::string nodeKey = nodePath.generic_string();
            auto closeCommitIt = treeCloseCommit_.find(nodeKey);
            if (closeCommitIt != treeCloseCommit_.end() && closeCommitIt->second) {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
                treeCloseCommit_.erase(closeCommitIt);
            } else {
                auto pendingIt = treePendingClose_.find(nodeKey);
                if (pendingIt != treePendingClose_.end() && pendingIt->second) {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                }
            }

            bool nodeOpenRaw = ImGui::TreeNodeEx(nodeLabel.c_str(), flags);
            if (ImGui::IsItemToggledOpen()) {
                treeExpandAnimTime_[nodeKey] = 0.0f;
                treeExpandAnimOpening_[nodeKey] = nodeOpenRaw;
                treeCloseCommit_.erase(nodeKey);
                if (!nodeOpenRaw && hasChildren) {
                    treePendingClose_[nodeKey] = true;
                } else {
                    treePendingClose_.erase(nodeKey);
                }
            }

            if (ImGui::IsItemClicked()) {
                NavigateToDirectory(nodePath);
            }

            bool pendingClose = false;
            auto pendingIt = treePendingClose_.find(nodeKey);
            if (pendingIt != treePendingClose_.end()) {
                pendingClose = pendingIt->second;
            }

            bool showChildren = hasChildren && (nodeOpenRaw || pendingClose);
            if (!showChildren) {
                return;
            }

            float childrenAlpha = updateExpandAlpha(nodePath);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * childrenAlpha);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (1.0f - childrenAlpha) * ImGui::GetTextLineHeightWithSpacing());

            if (nodeOpenRaw) {
                DrawFolderTree(nodePath);
                ImGui::TreePop();
            } else {
                ImGui::Indent();
                DrawFolderTree(nodePath);
                ImGui::Unindent();
            }

            ImGui::PopStyleVar();
        };

        try {
            if (path == rootPath_) {
                if (std::filesystem::exists(appAssetsPath_)) {
                    drawFolderNode(appAssetsPath_, "Application", isUnderPath(currentPath_, appAssetsPath_));
                }
                if (std::filesystem::exists(engineAssetsPath_)) {
                    drawFolderNode(engineAssetsPath_, "Engine", isUnderPath(currentPath_, engineAssetsPath_));
                }
                return;
            }

            std::vector<std::filesystem::path> directories;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    directories.push_back(entry.path());
                }
            }

            std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
                return a.filename().string() < b.filename().string();
                });

            for (const auto& dir : directories) {
                drawFolderNode(dir, dir.filename().string(), false);
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}", "Failed to draw folder tree: " + std::string(e.what()));
        }
    }

    void ProjectView::DrawBreadcrumb()
    {
        // 「Assets」ボタン（仮想ルートに戻る）
        if (ImGui::Button("Assets")) {
            NavigateToDirectory(rootPath_);
        }

        if (currentPath_ == rootPath_) return;

        // どちらのアセットルート配下か判定
        std::filesystem::path baseRoot;
        std::string rootLabel;
        if (currentPath_ == appAssetsPath_ ||
            (!std::filesystem::relative(currentPath_, appAssetsPath_).empty() &&
             std::filesystem::relative(currentPath_, appAssetsPath_).native()[0] != '.')) {
            baseRoot  = appAssetsPath_;
            rootLabel = "Application";
        } else {
            baseRoot  = engineAssetsPath_;
            rootLabel = "Engine";
        }

        ImGui::SameLine(); ImGui::Text(">"); ImGui::SameLine();
        if (ImGui::Button(rootLabel.c_str())) {
            NavigateToDirectory(baseRoot);
        }

        if (currentPath_ == baseRoot) return;

        // baseRoot 以降の各要素をボタン表示
        auto relativePath = std::filesystem::relative(currentPath_, baseRoot);
        std::filesystem::path currentDir = baseRoot;
        for (const auto& part : relativePath) {
            ImGui::SameLine(); ImGui::Text(">"); ImGui::SameLine();
            currentDir /= part;
            std::string buttonLabel = part.string();
            if (ImGui::Button(buttonLabel.c_str())) {
                NavigateToDirectory(currentDir);
            }
        }
    }

    void ProjectView::OpenInExplorer(const std::filesystem::path& filePath)
    {
        if (!std::filesystem::exists(filePath)) {
            return;
        }

        std::wstring explorerArg = L"/select,\"" + filePath.wstring() + L"\"";
        HINSTANCE result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe", explorerArg.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to open Explorer for: " + filePath.string());
        }
    }

    std::string ProjectView::GetRelativePath(const std::filesystem::path& fullPath)
    {
        std::string result = "Assets";

        // ルートパスと同じ場合は "Assets" のみ
        if (fullPath == rootPath_) {
            return result;
        }

        // 相対パスを取得
        auto relativePath = std::filesystem::relative(fullPath, rootPath_);

        // パスの各要素を " > " で連結
        for (const auto& part : relativePath) {
            result += " > " + part.string();
        }

        return result;
    }

    ProjectView::PNGPreviewInfo ProjectView::GetPNGPreview(const std::filesystem::path& filePath)
    {
        PNGPreviewInfo result;
        result.width = iconSize_;
        result.height = iconSize_;
        
        std::string pathStr = filePath.string();
        
        // キャッシュに存在する場合はそれを返す
        auto it = pngPreviewInfoCache_.find(pathStr);
        if (it != pngPreviewInfoCache_.end()) {
            return it->second;
        }
        
        try {
            auto& textureManager = TextureManager::GetInstance();
            
            // 相対パスに変換（プロジェクトルートから）
            std::filesystem::path relativePath = std::filesystem::relative(filePath, rootPath_);
            std::string relativePathStr = relativePath.generic_string();

            // テクスチャを読み込む
            auto texture = textureManager.Load(relativePathStr);

            if (texture.texture) {
                // メタデータを取得してアスペクト比を計算
                auto metadata = textureManager.GetMetadata(relativePathStr);
                float aspectRatio = static_cast<float>(metadata.width) / static_cast<float>(metadata.height);
                
                // アイコンサイズに収まるようにスケーリング
                if (aspectRatio > 1.0f) {
                    result.width = iconSize_;
                    result.height = iconSize_ / aspectRatio;
                } else {
                    result.width = iconSize_ * aspectRatio;
                    result.height = iconSize_;
                }
                
                result.gpuHandle = texture.gpuHandle;
                
                // キャッシュに保存
                pngPreviewCache_[pathStr] = texture.texture;
                pngPreviewInfoCache_[pathStr] = result;
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to load PNG preview: " + std::string(e.what()));
        }
        
        return result;
    }
}


