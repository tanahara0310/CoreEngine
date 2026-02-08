#include "ProjectView.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Utility/Logger/Logger.h"

#include <imgui.h>
#include <algorithm>

namespace CoreEngine
{
    void ProjectView::Initialize(DirectXCommon* dxCommon)
    {
        dxCommon_ = dxCommon;

        // ルートパスをAssetsフォルダに設定
        rootPath_ = std::filesystem::current_path() / "Assets";
        currentPath_ = rootPath_;

        // Assetsフォルダが存在しない場合は作成
        if (!std::filesystem::exists(rootPath_)) {
            std::filesystem::create_directories(rootPath_);
            Logger::GetInstance().Log("Created Assets folder: " + rootPath_.string(),
                LogLevel::INFO, LogCategory::System);
        }

        // デフォルトアイコンを読み込み
        LoadDefaultIcon();
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
                // 現在のディレクトリの内容を取得
                auto entries = GetCurrentDirectoryContents();

                // グリッドレイアウトで表示
                DrawGridLayout(entries);
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
        std::vector<Entry> entries;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentPath_)) {
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
            Logger::GetInstance().Log("Failed to read directory: " + std::string(e.what()),
                LogLevel::Error, LogCategory::System);
        }

        return entries;
    }

    void ProjectView::NavigateToDirectory(const std::filesystem::path& path)
    {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            currentPath_ = path;
            selectedIndex_ = -1;
        }
    }

    void ProjectView::NavigateUp()
    {
        if (currentPath_ != rootPath_ && currentPath_.has_parent_path()) {
            currentPath_ = currentPath_.parent_path();

            // ルートパスより上には行かない
            if (currentPath_.string().size() < rootPath_.string().size()) {
                currentPath_ = rootPath_;
            }

            selectedIndex_ = -1;
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
                        // ファイルの場合は将来的に開く処理を実装
                        Logger::GetInstance().Log("File selected: " + entry.name,
                            LogLevel::INFO, LogCategory::System);
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
            auto texture = textureManager.Load("EngineAssets/directoryIcon.png");
            if (texture.texture) {
                directoryIconTexture_ = texture.texture;
                directoryIconGpuHandle_ = texture.gpuHandle;
                directoryIconLoaded_ = true;
                Logger::GetInstance().Log("Loaded directory icon for ProjectView",
                    LogLevel::INFO, LogCategory::System);
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log("Failed to load directory icon: " + std::string(e.what()),
                LogLevel::WARNING, LogCategory::System);
            directoryIconLoaded_ = false;
        }
        
        // PNGアイコンを読み込み
        try {
            auto texture = textureManager.Load("EngineAssets/pngIcon.png");
            if (texture.texture) {
                pngIconTexture_ = texture.texture;
                pngIconGpuHandle_ = texture.gpuHandle;
                pngIconLoaded_ = true;
                Logger::GetInstance().Log("Loaded PNG icon for ProjectView",
                    LogLevel::INFO, LogCategory::System);
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log("Failed to load PNG icon: " + std::string(e.what()),
                LogLevel::WARNING, LogCategory::System);
            pngIconLoaded_ = false;
        }
        
        // ファイルアイコンを読み込み
        try {
            auto texture = textureManager.Load("EngineAssets/fileIcon.png");
            if (texture.texture) {
                fileIconTexture_ = texture.texture;
                fileIconGpuHandle_ = texture.gpuHandle;
                fileIconLoaded_ = true;
                Logger::GetInstance().Log("Loaded file icon for ProjectView",
                    LogLevel::INFO, LogCategory::System);
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log("Failed to load file icon: " + std::string(e.what()),
                LogLevel::WARNING, LogCategory::System);
            fileIconLoaded_ = false;
        }
    }

    void ProjectView::DrawFolderTree(const std::filesystem::path& path)
    {
        try {
            // ディレクトリのみを取得してソート
            std::vector<std::filesystem::path> directories;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    directories.push_back(entry.path());
                }
            }

            std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
                return a.filename().string() < b.filename().string();
                });

            // 各ディレクトリをツリーノードとして表示
            for (const auto& dir : directories) {
                std::string folderName = dir.filename().string();

                // ツリーノードのフラグ
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

                // 現在のパスと一致する場合は選択状態にする
                if (dir == currentPath_) {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }

                // サブディレクトリがあるかチェック
                bool hasSubdirectories = false;
                for (const auto& subEntry : std::filesystem::directory_iterator(dir)) {
                    if (subEntry.is_directory()) {
                        hasSubdirectories = true;
                        break;
                    }
                }

                // サブディレクトリがない場合はリーフノードとして表示
                if (!hasSubdirectories) {
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }

                // ツリーノードを描画
                bool nodeOpen = ImGui::TreeNodeEx(folderName.c_str(), flags);

                // クリックされた場合、そのディレクトリに移動
                if (ImGui::IsItemClicked()) {
                    NavigateToDirectory(dir);
                }

                // ノードが開かれていて、サブディレクトリがある場合は再帰的に描画
                if (nodeOpen && hasSubdirectories) {
                    DrawFolderTree(dir);
                    ImGui::TreePop();
                }
            }
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log("Failed to draw folder tree: " + std::string(e.what()),
                LogLevel::Error, LogCategory::System);
        }
    }

    void ProjectView::DrawBreadcrumb()
    {
        // 相対パスを取得
        std::filesystem::path currentDir = rootPath_;
        
        // "Assets" ボタン
        if (ImGui::Button("Assets")) {
            NavigateToDirectory(rootPath_);
        }
        
        // ルートパスと同じ場合は終了
        if (currentPath_ == rootPath_) {
            return;
        }
        
        // 相対パスを取得してパスの各要素をボタンで表示
        auto relativePath = std::filesystem::relative(currentPath_, rootPath_);
        
        for (const auto& part : relativePath) {
            ImGui::SameLine();
            ImGui::Text(">");
            ImGui::SameLine();
            
            // 現在のディレクトリパスを構築
            currentDir /= part;
            std::string buttonLabel = part.string();
            
            // ボタンをクリックするとそのディレクトリに移動
            if (ImGui::Button(buttonLabel.c_str())) {
                NavigateToDirectory(currentDir);
            }
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
            
            // 相対パスに変換（Assetsフォルダからの相対パス）
            std::filesystem::path relativePath = std::filesystem::relative(filePath, rootPath_.parent_path());
            
            // テクスチャを読み込む
            auto texture = textureManager.Load(relativePath.string());
            
            if (texture.texture) {
                // メタデータを取得してアスペクト比を計算
                auto metadata = textureManager.GetMetadata(relativePath.string());
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
            Logger::GetInstance().Log("Failed to load PNG preview: " + std::string(e.what()),
                LogLevel::WARNING, LogCategory::System);
        }
        
        return result;
    }
}
