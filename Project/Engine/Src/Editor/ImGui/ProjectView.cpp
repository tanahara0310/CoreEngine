#include "pch.h"
#include "ProjectView.h"
#include "Utility/Path/ProjectPaths.h"
#include "Editor/External/ExternalCodeEditor.h"
#include "Editor/Script/ScriptTemplate.h"
#include "Editor/Project/AssetFileOperations.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/Logger/Logger.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#endif
#include <algorithm>
#include <array>
#include <fstream>
#include <format>
#include <Windows.h>
#include <shellapi.h>

#ifdef CORE_EDITOR
namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// @brief 種類ごとの記号と色
        struct KindStyle
        {
            const char* glyph;
            const char* label;
            ImVec4 color;
        };

        KindStyle StyleOf(ProjectView::Kind kind)
        {
            switch (kind) {
            case ProjectView::Kind::Folder:   return { "▸", "フォルダ",   Theme::kTextDim };
            case ProjectView::Kind::Model:    return { "▣", "モデル",     Theme::kAccentHover };
            case ProjectView::Kind::Texture:  return { "▤", "テクスチャ", Theme::kAccentHover };
            case ProjectView::Kind::Prefab:   return { "◈", "プレハブ",   Theme::kAccent };
            case ProjectView::Kind::Script:   return { "◇", "スクリプト", Theme::kScript };
            case ProjectView::Kind::Scene:    return { "▦", "シーン",     Theme::kWarm };
            case ProjectView::Kind::Audio:    return { "♪", "音",         Theme::kOk };
            case ProjectView::Kind::Material: return { "◍", "マテリアル", Theme::kTextDim };
            case ProjectView::Kind::Shader:   return { "▧", "シェーダ",   Theme::kTextDim };
            case ProjectView::Kind::Data:     return { "▤", "データ",     Theme::kTextMute };
            default: break;
            }
            return { "▫", "その他", Theme::kTextMute };
        }

        /// @brief 絞り込みに出す種類の並び
        constexpr std::array<ProjectView::Kind, 7> kFilterKinds = {
            ProjectView::Kind::Model,
            ProjectView::Kind::Texture,
            ProjectView::Kind::Prefab,
            ProjectView::Kind::Script,
            ProjectView::Kind::Scene,
            ProjectView::Kind::Audio,
            ProjectView::Kind::Data,
        };

        /// @brief 拡張子から種類を決める
        ProjectView::Kind ClassifyKind(const std::filesystem::path& path, bool isDirectory)
        {
            if (isDirectory) {
                return ProjectView::Kind::Folder;
            }

            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(::tolower(c)); });

            if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                return ProjectView::Kind::Model;
            }
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp"
                || ext == ".tga" || ext == ".dds" || ext == ".hdr") {
                return ProjectView::Kind::Texture;
            }
            if (ext == ".prefab") {
                return ProjectView::Kind::Prefab;
            }
            if (ext == ".as") {
                return ProjectView::Kind::Script;
            }
            if (ext == ".scene") {
                return ProjectView::Kind::Scene;
            }
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
                return ProjectView::Kind::Audio;
            }
            if (ext == ".mtl" || ext == ".mat" || ext == ".material") {
                return ProjectView::Kind::Material;
            }
            if (ext == ".hlsl" || ext == ".hlsli" || ext == ".cso") {
                return ProjectView::Kind::Shader;
            }
            if (ext == ".json" || ext == ".csv") {
                return ProjectView::Kind::Data;
            }
            return ProjectView::Kind::Other;
        }

        /// @brief 小文字にした文字列
        std::string ToLower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            return text;
        }
    }

    void ProjectView::Initialize(GraphicsCore* dxCommon)
    {
        dxCommon_ = dxCommon;

        // ルートパスをプロジェクトルート（仮想ルート）に設定
        rootPath_ = ProjectPaths::Root();
        appAssetsPath_ = rootPath_ / "Application" / "Assets";
        engineAssetsPath_ = rootPath_ / "Engine" / "Assets";
        currentPath_ = rootPath_;

        currentEntries_ = GetCurrentDirectoryContents();
    }

    void ProjectView::Update()
    {
        if (!isVisible_) {
            return;
        }

        // AssetDatabase に登録が増えたら、表示中のフォルダを読み直す
        const uint64_t assetRevision = AssetDatabase::GetInstance().GetRevision();
        if (assetRevision != seenAssetRevision_) {
            seenAssetRevision_ = assetRevision;
            currentEntries_ = GetCurrentDirectoryContents();
            hasSubdirCache_.clear();
            treeDirCache_.clear();
        }

        // ウィンドウを開始
        if (ImGui::Begin("Project", &isVisible_, ImGuiWindowFlags_NoCollapse)) {

            // パンくずリスト表示
            DrawBreadcrumb();

            // 検索と種類の絞り込み
            DrawFilterBar();

            UI::Separator();

            // 2カラムレイアウト（左：フォルダツリー、右：グリッドビュー）
            ImGui::BeginChild("LeftPanel", ImVec2(treeViewWidth_, 0.0f), true);
            {
                // フォルダツリーを描画
                DrawFolderTree(rootPath_);
            }
            ImGui::EndChild();

            UI::SameLine();

            HandleFileShortcuts();

            // 右側の一覧
            ImGui::BeginChild("RightPanel", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            {
                const std::vector<Entry> shown = FilterEntries(currentEntries_);
                if (useListView_) {
                    DrawListLayout(shown);
                } else {
                    DrawGridLayout(shown);
                }
                DrawCreateContextMenu();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        DrawNewScriptDialog();
        DrawRenameDialog();
        DrawDeleteDialog();

        // 描画中に頼まれた移動をここで行う
        ApplyPendingNavigation();
    }

    void ProjectView::DrawFilterBar()
    {
        // 1 行目：名前の絞り込みと表示形式
        const float toggleWidth = UI::Bar::ButtonWidth("グリッド") + UI::Bar::ButtonWidth("一覧") + 6.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - toggleWidth - 8.0f);
        ImGui::InputTextWithHint("##project_search", "名前で絞り込み...",
            searchFilter_, sizeof(searchFilter_));

        UI::SameLine(0.0f, 8.0f);
        if (UI::Bar::Button("＋ 作成", false, "スクリプトを作る")) { OpenNewScriptDialog(); }
        UI::SameLine(0.0f, 8.0f);
        if (UI::Bar::Button("グリッド", !useListView_)) { useListView_ = false; }
        UI::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("一覧", useListView_)) { useListView_ = true; }

        // 2 行目：種類の絞り込み（幅が足りなくなったら折り返す）
        const auto keepOnLine = [](float width) {
            const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (ImGui::GetItemRectMax().x + 4.0f + width < rightEdge) {
                UI::SameLine(0.0f, 4.0f);
            }
            };

        if (UI::Bar::Button("すべて", kindFilter_ == Kind::Any)) {
            kindFilter_ = Kind::Any;
        }
        for (const Kind kind : kFilterKinds) {
            const KindStyle style = StyleOf(kind);
            keepOnLine(UI::Bar::ButtonWidth(style.label));
            if (UI::Bar::Button(style.label, kindFilter_ == kind)) {
                kindFilter_ = (kindFilter_ == kind) ? Kind::Any : kind;
            }
        }

        const std::string countText = std::format("{} 件", currentEntries_.size());
        keepOnLine(ImGui::CalcTextSize(countText.c_str()).x + 8.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kTextMute, "%s", countText.c_str());
    }

    std::vector<ProjectView::Entry> ProjectView::FilterEntries(const std::vector<Entry>& entries) const
    {
        const std::string query = ToLower(searchFilter_);
        if (query.empty() && kindFilter_ == Kind::Any) {
            return entries;
        }

        std::vector<Entry> result;
        for (const Entry& entry : entries) {
            // フォルダは種類で絞っても残す（潜れなくなるため）
            if (kindFilter_ != Kind::Any && !entry.isDirectory && entry.kind != kindFilter_) {
                continue;
            }
            if (!query.empty() && ToLower(entry.name).find(query) == std::string::npos) {
                continue;
            }
            result.push_back(entry);
        }
        return result;
    }

    void ProjectView::Finalize()
    {
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
                e.kind = Kind::Folder;
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
                e.name = Logger::GetInstance().PathToUtf8(entry.path().filename());
                e.path = entry.path();
                e.isDirectory = entry.is_directory();
                e.kind = ClassifyKind(e.path, e.isDirectory);
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
        // 実際の移動はフレームの最後まで遅らせる。
        // 移動はフォルダツリーのキャッシュを捨てるので、ツリーをなぞっている最中に
        // 呼ぶと、走査中の配列がその場で消える。
        pendingNavigate_ = path;
    }

    void ProjectView::ApplyPendingNavigation()
    {
        if (pendingNavigate_.empty()) {
            return;
        }

        const std::filesystem::path path = pendingNavigate_;
        pendingNavigate_.clear();

        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path) || path == currentPath_) {
            return;
        }

        currentPath_ = path;
        currentEntries_ = GetCurrentDirectoryContents();
        selectedIndex_ = -1;
        hasSubdirCache_.clear();
        treeDirCache_.clear();
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

    void ProjectView::RebuildReferences(const std::filesystem::path& assetPath)
    {
        referencesFor_ = assetPath;
        references_.clear();
        if (assetPath.empty() || std::filesystem::is_directory(assetPath)) {
            return;
        }

        // GUID とファイル名の両方で探す（参照はどちらの形でも書かれるため）
        std::vector<std::string> needles;
        if (const std::string guid = AssetDatabase::GetInstance().GetGUID(assetPath); !guid.empty()) {
            needles.push_back(guid);
        }
        needles.push_back(Logger::GetInstance().PathToUtf8(assetPath.filename()));

        // 参照を書ける形式のファイルだけを開く
        const auto isSearchable = [](const std::filesystem::path& path) {
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            return ext == ".json" || ext == ".prefab" || ext == ".as" || ext == ".csv" || ext == ".mtl";
            };

        std::error_code error;
        for (const std::filesystem::path& root : { appAssetsPath_, engineAssetsPath_ }) {
            if (!std::filesystem::exists(root, error)) {
                continue;
            }
            for (std::filesystem::recursive_directory_iterator it(root, error), last; it != last; it.increment(error)) {
                if (error) {
                    break;
                }
                if (!it->is_regular_file(error) || !isSearchable(it->path()) || it->path() == assetPath) {
                    continue;
                }

                std::ifstream file(it->path(), std::ios::binary);
                if (!file) {
                    continue;
                }
                const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                for (const std::string& needle : needles) {
                    if (text.find(needle) != std::string::npos) {
                        references_.push_back({ it->path(), ClassifyKind(it->path(), false) });
                        break;
                    }
                }
            }
        }

        std::sort(references_.begin(), references_.end(),
            [](const Reference& a, const Reference& b) { return a.path < b.path; });
    }

    void ProjectView::DrawSelectedAssetInspector()
    {
        if (selectedPath_.empty()) {
            return;
        }

        const bool isDirectory = std::filesystem::is_directory(selectedPath_);
        const KindStyle style = StyleOf(ClassifyKind(selectedPath_, isDirectory));
        const std::string name = Logger::GetInstance().PathToUtf8(selectedPath_.filename());

        ImGui::TextColored(style.color, "%s", style.glyph);
        UI::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(name.c_str());
        UI::SameLine(0.0f, 10.0f);
        ImGui::TextColored(Theme::kTextMute, "%s", style.label);

        if (isDirectory) {
            return;
        }

        UI::Separator();
        UI::SectionHeader("アセット情報");

        const std::string guid = AssetDatabase::GetInstance().GetGUID(selectedPath_);
        const auto field = [](const char* label, const std::string& value, const ImVec4& color) {
            ImGui::TextColored(Theme::kTextDim, "%s", label);
            UI::SameLine(110.0f);
            ImGui::TextColored(color, "%s", value.c_str());
            };

        field("GUID", guid.empty() ? "—（登録されていません）" : guid,
            guid.empty() ? Theme::kTextMute : Theme::kText);
        if (!guid.empty() && ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(guid.c_str());
        }
        if (!guid.empty()) {
            UI::Tooltip("クリックで GUID をコピー");
        }

        field("パス", GetRelativePath(selectedPath_), Theme::kText);

        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(selectedPath_, error);
        field("サイズ", error ? std::string("—")
            : (size >= 1024 * 1024 ? std::format("{:.1f} MB", static_cast<double>(size) / (1024.0 * 1024.0))
                : std::format("{:.1f} KB", static_cast<double>(size) / 1024.0)),
            Theme::kText);

        // 参照している場所（選択が変わったときだけ探し直す）
        if (referencesFor_ != selectedPath_) {
            RebuildReferences(selectedPath_);
        }

        UI::Separator();
        UI::SectionHeader("参照している場所");

        if (references_.empty()) {
            UI::Hint("どこからも参照されていません");
            return;
        }

        for (size_t i = 0; i < references_.size(); ++i) {
            const Reference& reference = references_[i];
            const KindStyle referenceStyle = StyleOf(reference.kind);

            ImGui::PushID(static_cast<int>(i));
            ImGui::TextColored(referenceStyle.color, "%s", referenceStyle.glyph);
            UI::SameLine(0.0f, 6.0f);
            if (ImGui::Selectable(GetRelativePath(reference.path).c_str())) {
                // その場所を Project で開く
                NavigateToDirectory(reference.path.parent_path());
                selectedPath_ = reference.path;
            }
            ImGui::PopID();
        }
    }

    void ProjectView::DrawGridLayout(const std::vector<Entry>& entries)
    {
        if (entries.empty()) {
            UI::Hint("該当するアセットがありません");
            return;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        const float itemWidth = iconSize_ + padding_ * 2.0f;
        const float itemHeight = iconSize_ + textHeight_ + padding_ * 2.0f;

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];

            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();

            // 選択中の下地
            if (entry.path == selectedPath_) {
                const ImVec2 rectMin = ImGui::GetCursorScreenPos();
                const ImVec2 rectMax = ImVec2(rectMin.x + itemWidth, rectMin.y + itemHeight);
                ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax,
                    ImGui::GetColorU32(Theme::kAccentMuted), 4.0f);
            }

            ImGui::Dummy(ImVec2(padding_, padding_));
            DrawIcon(entry);

            // 名前は 2 行まで。あふれる分は省略する
            std::string displayName = entry.name;
            const float textWidth = itemWidth - padding_ * 2.0f;
            const float maxHeight = ImGui::GetTextLineHeight() * 2.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textWidth);
            if (ImGui::CalcTextSize(displayName.c_str(), nullptr, false, textWidth).y > maxHeight) {
                for (size_t maxLen = displayName.size(); maxLen > 3; --maxLen) {
                    const std::string truncated = displayName.substr(0, maxLen - 3) + "...";
                    if (ImGui::CalcTextSize(truncated.c_str(), nullptr, false, textWidth).y <= maxHeight) {
                        displayName = truncated;
                        break;
                    }
                }
            }
            const ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str(), nullptr, false, textWidth);
            if (const float textOffsetX = (itemWidth - textSize.x) * 0.5f; textOffsetX > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
            }
            ImGui::TextUnformatted(displayName.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndGroup();

            if (ImGui::IsItemHovered() && displayName != entry.name) {
                ImGui::SetTooltip("%s", entry.name.c_str());
            }
            HandleEntryInteraction(entry, static_cast<int>(i));

            // 次の項目を同じ行に配置（ウィンドウ幅を超える場合は改行）
            const float nextItemX = ImGui::GetItemRectMax().x + style.ItemSpacing.x + itemWidth;
            if (i + 1 < entries.size() && nextItemX < windowVisibleX) {
                UI::SameLine();
            }

            ImGui::PopID();
        }
    }

    void ProjectView::DrawListLayout(const std::vector<Entry>& entries)
    {
        if (entries.empty()) {
            UI::Hint("該当するアセットがありません");
            return;
        }

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
            | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_ScrollY;

        if (auto table = UI::Scope::TableScope("##project_list", 3, flags)) {
            ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("種類", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableHeadersRow();

            AssetDatabase& assets = AssetDatabase::GetInstance();
            for (size_t i = 0; i < entries.size(); ++i) {
                const Entry& entry = entries[i];
                const KindStyle style = StyleOf(entry.kind);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));

                ImGui::TextColored(style.color, "%s", style.glyph);
                UI::SameLine(0.0f, 6.0f);
                ImGui::Selectable(entry.name.c_str(), entry.path == selectedPath_,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                HandleEntryInteraction(entry, static_cast<int>(i));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(Theme::kTextMute, "%s", style.label);

                ImGui::TableSetColumnIndex(2);
                const std::string guid = entry.isDirectory ? std::string{} : assets.GetGUID(entry.path);
                ImGui::TextColored(Theme::kTextMute, "%s", guid.empty() ? "—" : guid.c_str());

                ImGui::PopID();
            }
        }
    }

    void ProjectView::HandleEntryInteraction(const Entry& entry, int index)
    {
        // シングルクリックで選択、ダブルクリックで潜る / 開く
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            const double now = ImGui::GetTime();
            if (lastClickedIndex_ == index && (now - lastClickTime_) < 0.3) {
                if (entry.isDirectory) {
                    NavigateToDirectory(entry.path);
                } else {
                    OpenFile(entry.path);
                }
                lastClickedIndex_ = -1;
                lastClickTime_ = 0.0;
            } else {
                selectedIndex_ = index;
                selectedPath_ = entry.path;
                lastClickedIndex_ = index;
                lastClickTime_ = now;
            }
        }

        DrawEntryContextMenu(entry);

        if (entry.isDirectory) {
            return;
        }

        // 受け口の型は種類で決まる
        const char* payloadType = nullptr;
        const char* payloadLabel = nullptr;
        switch (entry.kind) {
        case Kind::Texture: payloadType = "TEXTURE_FILE"; payloadLabel = "テクスチャ"; break;
        case Kind::Model:   payloadType = "MODEL_FILE";   payloadLabel = "モデル";     break;
        case Kind::Audio:   payloadType = "AUDIO_FILE";   payloadLabel = "音";         break;
        case Kind::Prefab:  payloadType = "PREFAB_FILE";  payloadLabel = "プレハブ";   break;
        default: break;
        }
        if (!payloadType) {
            return;
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string filename = Logger::GetInstance().PathToUtf8(entry.path.filename());
            ImGui::SetDragDropPayload(payloadType, filename.c_str(), filename.size() + 1);
            const KindStyle style = StyleOf(entry.kind);
            ImGui::TextColored(style.color, "%s", style.glyph);
            UI::SameLine(0.0f, 6.0f);
            ImGui::Text("%s: %s", payloadLabel, filename.c_str());
            ImGui::EndDragDropSource();
        }
    }

    void ProjectView::DrawIcon(const Entry& entry)
    {
        // テクスチャは実物を縮めて出す
        if (entry.kind == Kind::Texture) {
            const PNGPreviewInfo preview = GetPNGPreview(entry.path);
            if (preview.gpuHandle.ptr != 0) {
                const float offsetX = (iconSize_ - preview.width) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding_
                    + (offsetX > 0.0f ? offsetX : 0.0f));
                ImGui::Image((ImTextureID)preview.gpuHandle.ptr,
                    ImVec2(preview.width, preview.height));
                return;
            }
        }

        const KindStyle style = StyleOf(entry.kind);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 min(origin.x + padding_, origin.y);
        const ImVec2 max(min.x + iconSize_, min.y + iconSize_);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        if (entry.isDirectory) {
            // フォルダは形で描く（つまみ＋本体）
            const float tabWidth = iconSize_ * 0.42f;
            const float tabHeight = iconSize_ * 0.14f;
            const float top = min.y + iconSize_ * 0.18f;
            const ImU32 color = ImGui::GetColorU32(Theme::kWarm);
            draw->AddRectFilled(ImVec2(min.x + 6.0f, top),
                ImVec2(min.x + 6.0f + tabWidth, top + tabHeight * 2.0f), color, 3.0f);
            draw->AddRectFilled(ImVec2(min.x + 6.0f, top + tabHeight),
                ImVec2(max.x - 6.0f, max.y - iconSize_ * 0.18f), color, 4.0f);
        } else {
            // ファイルは種類ごとの記号を沈んだ面の上に描く
            draw->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kField), 6.0f);
            draw->AddRect(min, max, ImGui::GetColorU32(Theme::kOutline), 6.0f);

            const float glyphSize = iconSize_ * 0.5f;
            const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, style.glyph);
            draw->AddText(ImGui::GetFont(), glyphSize,
                ImVec2(min.x + (iconSize_ - measured.x) * 0.5f, min.y + (iconSize_ - measured.y) * 0.5f),
                ImGui::GetColorU32(style.color), style.glyph);
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding_);
        ImGui::Dummy(ImVec2(iconSize_, iconSize_));
    }

    void ProjectView::DrawFolderTree(const std::filesystem::path& path, int depth)
    {
        // ツリーの枝線は ImGui が描かないので、DrawList へ自前で引く
        const ImU32 treeLineColor = ImGui::GetColorU32(ImVec4(0.40f, 0.40f, 0.40f, 1.0f));

        // 開閉アニメーションの α を進めて返す。終わったら状態を捨てる
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
                    return 0.0f;
                }
                return 1.0f;
            }

            return alpha;
        };

        auto hasSubdirectories = [this](const std::filesystem::path& dirPath) {
            std::string key = dirPath.generic_string();
            auto it = hasSubdirCache_.find(key);
            if (it != hasSubdirCache_.end()) {
                return it->second;
            }
            bool result = false;
            try {
                for (const auto& subEntry : std::filesystem::directory_iterator(dirPath)) {
                    if (subEntry.is_directory()) {
                        result = true;
                        break;
                    }
                }
            } catch (...) {}
            hasSubdirCache_[key] = result;
            return result;
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

            ImVec2 cursorBeforeNode = ImGui::GetCursorScreenPos();
            bool nodeOpenRaw = ImGui::TreeNodeEx(nodeLabel.c_str(), flags);

            if (depth > 0) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float centerY = (itemMin.y + itemMax.y) * 0.5f;
                float indentSpacing = ImGui::GetStyle().IndentSpacing;
                float branchX = cursorBeforeNode.x - indentSpacing * 0.5f;
                float textStartX = itemMin.x - 4.0f;
                drawList->AddLine(ImVec2(branchX, centerY), ImVec2(textStartX, centerY), treeLineColor, 1.0f);
            // 開閉が切り替わった瞬間にアニメーションの起点を記録する
            }
            if (ImGui::IsItemToggledOpen()) {
                treeExpandAnimTime_[nodeKey] = 0.0f;
                treeExpandAnimOpening_[nodeKey] = nodeOpenRaw;
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
            // 閉じるアニメーション中は子を描き続ける必要があるので、
            // ImGui の開閉状態（nodeOpenRaw）とは別に pendingClose を見る
            }

            float childrenAlpha = updateExpandAlpha(nodePath);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * childrenAlpha);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (1.0f - childrenAlpha) * ImGui::GetTextLineHeightWithSpacing());

            if (nodeOpenRaw) {
                DrawFolderTree(nodePath, depth + 1);
                ImGui::TreePop();
            } else {
                ImGui::Indent();
                DrawFolderTree(nodePath, depth + 1);
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

            std::string cacheKey = path.generic_string();
            auto cacheIt = treeDirCache_.find(cacheKey);
            if (cacheIt == treeDirCache_.end()) {
                std::vector<std::filesystem::path> dirs;
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_directory()) {
                        dirs.push_back(entry.path());
                    }
                }
                std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
                    return a.filename().string() < b.filename().string();
                });
                treeDirCache_[cacheKey] = std::move(dirs);
                cacheIt = treeDirCache_.find(cacheKey);
            // 兄弟ノードを縦線でつなぐため、描画中に上下端を集めておく
            }
            const std::vector<std::filesystem::path>& directories = cacheIt->second;

            bool hasVerticalLineRange = false;
            float verticalLineX = 0.0f;
            float verticalLineMinY = 0.0f;
            float verticalLineMaxY = 0.0f;
            float indentSpacing = ImGui::GetStyle().IndentSpacing;

            for (const auto& dir : directories) {
                ImVec2 nodePosBefore = ImGui::GetCursorScreenPos();
                drawFolderNode(dir, dir.filename().string(), false);

                if (depth > 0) {
                    ImVec2 itemMin = ImGui::GetItemRectMin();
                    ImVec2 itemMax = ImGui::GetItemRectMax();
                    float branchX = nodePosBefore.x - indentSpacing * 0.5f;

                    if (!hasVerticalLineRange) {
                        hasVerticalLineRange = true;
                        verticalLineX = branchX;
                        verticalLineMinY = itemMin.y;
                        verticalLineMaxY = itemMax.y;
                    } else {
                        verticalLineMinY = (std::min)(verticalLineMinY, itemMin.y);
                        verticalLineMaxY = (std::max)(verticalLineMaxY, itemMax.y);
                    }
                }
            }

            if (hasVerticalLineRange) {
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(verticalLineX, verticalLineMinY),
                    ImVec2(verticalLineX, verticalLineMaxY),
                    treeLineColor,
                    1.0f);
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

        UI::SameLine(); ImGui::Text(">"); UI::SameLine();
        if (ImGui::Button(rootLabel.c_str())) {
            NavigateToDirectory(baseRoot);
        }

        if (currentPath_ == baseRoot) return;

        // baseRoot 以降の各要素をボタン表示
        auto relativePath = std::filesystem::relative(currentPath_, baseRoot);
        std::filesystem::path currentDir = baseRoot;
        for (const auto& part : relativePath) {
            UI::SameLine(); ImGui::Text(">"); UI::SameLine();
            currentDir /= part;
            std::string buttonLabel = part.string();
            if (ImGui::Button(buttonLabel.c_str())) {
                NavigateToDirectory(currentDir);
            }
        }
    }

    std::filesystem::path ProjectView::GetCurrentFolder() const
    {
        return currentPath_.lexically_relative(rootPath_);
    }

    void ProjectView::OpenFolder(const std::filesystem::path& relativeFolder)
    {
        if (relativeFolder.empty() || relativeFolder.is_absolute()) {
            return;
        }
        const std::filesystem::path relative = relativeFolder.lexically_normal();
        if (!relative.empty() && *relative.begin() == "..") {
            return;
        }
        const std::filesystem::path folder = (rootPath_ / relative).lexically_normal();
        std::error_code error;
        if (std::filesystem::is_directory(folder, error)) {
            NavigateToDirectory(folder);
        }
    }

    void ProjectView::DrawEntryContextMenu(const Entry& entry)
    {
        if (!ImGui::BeginPopupContextItem("##entryMenu")) {
            return;
        }

        // メニューを開いた項目を選択にしておく（見ているものと操作の対象をそろえる）
        selectedPath_ = entry.path;

        ImGui::TextDisabled("%s", entry.name.c_str());
        ImGui::Separator();

        if (ImGui::MenuItem("コピー", "Ctrl+C")) {
            clipboardPath_ = entry.path;
            clipboardIsCut_ = false;
        }
        if (ImGui::MenuItem("切り取り", "Ctrl+X")) {
            clipboardPath_ = entry.path;
            clipboardIsCut_ = true;
        }
        if (ImGui::MenuItem("貼り付け", "Ctrl+V", false, !clipboardPath_.empty())) {
            PasteIntoCurrentFolder();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("名前を変更", "F2")) {
            OpenRenameDialog(entry.path);
        }
        if (ImGui::MenuItem("削除", "Delete")) {
            OpenDeleteDialog(entry.path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("エクスプローラで開く")) {
            const std::wstring folder = entry.isDirectory
                ? entry.path.wstring() : entry.path.parent_path().wstring();
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        ImGui::EndPopup();
    }

    void ProjectView::HandleFileShortcuts()
    {
        // Project の窓にフォーカスがあるときだけ効かせる（他の窓の Ctrl+C を奪わない）
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            return;
        }
        if (showRenameDialog_ || showDeleteDialog_ || showNewScriptDialog_) {
            return;
        }

        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedPath_.empty()) {
            clipboardPath_ = selectedPath_;
            clipboardIsCut_ = false;
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedPath_.empty()) {
            clipboardPath_ = selectedPath_;
            clipboardIsCut_ = true;
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboardPath_.empty()) {
            PasteIntoCurrentFolder();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && !selectedPath_.empty()) {
            OpenRenameDialog(selectedPath_);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedPath_.empty()) {
            OpenDeleteDialog(selectedPath_);
        }
    }

    void ProjectView::PasteIntoCurrentFolder()
    {
        if (clipboardPath_.empty()) {
            return;
        }

        std::filesystem::path result;
        std::string error;
        const bool ok = clipboardIsCut_
            ? Editor::AssetFileOperations::Move(clipboardPath_, currentPath_, &result, &error)
            : Editor::AssetFileOperations::Copy(clipboardPath_, currentPath_, &result, &error);

        if (ok) {
            if (clipboardIsCut_) {
                clipboardPath_.clear();  // 切り取りは 1 回だけ
            }
            selectedPath_ = result;
            currentEntries_ = GetCurrentDirectoryContents();
        } else {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "貼り付けできません: {}", error);
        }
    }

    void ProjectView::OpenRenameDialog(const std::filesystem::path& target)
    {
        showRenameDialog_ = true;
        renameTarget_ = target;
        renameError_.clear();

        const std::string name = Logger::GetInstance().PathToUtf8(target.filename());
        const std::size_t length = (std::min)(name.size(), sizeof(renameBuffer_) - 1);
        std::memcpy(renameBuffer_, name.data(), length);
        renameBuffer_[length] = '\0';
    }

    void ProjectView::DrawRenameDialog()
    {
        constexpr const char* kTitle = "名前を変更";
        if (showRenameDialog_ && !ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        UI::Hint("実物のファイルの名前が変わります。GUID を持つ .meta も一緒に付いていきます。");
        if (renameTarget_.extension() == ".as") {
            UI::Hint("スクリプトはクラス名とファイル名を合わせてください（中のクラス名は変わりません）。");
        }
        UI::Separator();

        const bool enterPressed = UI::InputText("新しい名前", renameBuffer_, sizeof(renameBuffer_),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (!renameError_.empty()) {
            ImGui::TextColored(Theme::kError, "%s", renameError_.c_str());
        }

        UI::Separator();
        const bool commit = ImGui::Button("変更") || enterPressed;
        if (commit) {
            std::filesystem::path result;
            if (Editor::AssetFileOperations::Rename(renameTarget_, renameBuffer_, &result, &renameError_)) {
                showRenameDialog_ = false;
                ImGui::CloseCurrentPopup();
                selectedPath_ = result;
                currentEntries_ = GetCurrentDirectoryContents();
            }
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            showRenameDialog_ = false;
            renameError_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectView::OpenDeleteDialog(const std::filesystem::path& target)
    {
        showDeleteDialog_ = true;
        deleteTarget_ = target;
        deleteError_.clear();
    }

    void ProjectView::DrawDeleteDialog()
    {
        constexpr const char* kTitle = "削除";
        if (showDeleteDialog_ && !ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        std::error_code ec;
        const bool isDirectory = std::filesystem::is_directory(deleteTarget_, ec);
        ImGui::Text("%s を削除しますか？",
            Logger::GetInstance().PathToUtf8(deleteTarget_.filename()).c_str());
        if (isDirectory) {
            ImGui::TextColored(Theme::kError, "フォルダの中身もまとめて消えます。");
        }
        UI::Hint("ごみ箱へ送るので、間違えたらエクスプローラのごみ箱から戻せます。");

        if (!deleteError_.empty()) {
            ImGui::TextColored(Theme::kError, "%s", deleteError_.c_str());
        }

        UI::Separator();
        if (ImGui::Button("削除")) {
            if (Editor::AssetFileOperations::MoveToRecycleBin(deleteTarget_, &deleteError_)) {
                showDeleteDialog_ = false;
                ImGui::CloseCurrentPopup();
                if (selectedPath_ == deleteTarget_) { selectedPath_.clear(); }
                if (clipboardPath_ == deleteTarget_) { clipboardPath_.clear(); }
                currentEntries_ = GetCurrentDirectoryContents();
            }
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            showDeleteDialog_ = false;
            deleteError_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectView::DrawCreateContextMenu()
    {
        if (!ImGui::BeginPopupContextWindow("##projectCreate",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            return;
        }
        if (ImGui::MenuItem("スクリプトを作成...")) {
            OpenNewScriptDialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("貼り付け", "Ctrl+V", false, !clipboardPath_.empty())) {
            PasteIntoCurrentFolder();
        }
        ImGui::EndPopup();
    }

    void ProjectView::OpenNewScriptDialog()
    {
        showNewScriptDialog_ = true;
        newScriptError_.clear();
        newScriptName_[0] = '\0';

        // 置き先は今開いているフォルダ。スクリプトのフォルダの外にいるならその根を出す
        const std::filesystem::path scriptRoot = Editor::ScriptTemplate::GetScriptRoot();
        std::filesystem::path folder = GetCurrentFolder().lexically_normal();
        const std::filesystem::path fromRoot = folder.lexically_relative(scriptRoot);
        if (fromRoot.empty() || *fromRoot.begin() == "..") {
            folder = scriptRoot;
        }
        const std::string text = folder.generic_string();
        const std::size_t length = (std::min)(text.size(), sizeof(newScriptFolder_) - 1);
        std::memcpy(newScriptFolder_, text.data(), length);
        newScriptFolder_[length] = '\0';

        if (newScriptTemplate_.empty()) {
            const std::vector<Editor::ScriptTemplate::Entry> templates = Editor::ScriptTemplate::List();
            newScriptTemplate_ = templates.empty() ? std::string("Basic") : templates.front().id;
            for (const auto& entry : templates) {
                if (entry.id == "Basic") { newScriptTemplate_ = entry.id; break; }
            }
        }
    }

    void ProjectView::DrawNewScriptDialog()
    {
        constexpr const char* kTitle = "新しいスクリプト";
        if (showNewScriptDialog_ && !ImGui::IsPopupOpen(kTitle)) {
            ImGui::OpenPopup(kTitle);
        }

        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        UI::Hint("クラス名がそのままファイル名とコンポーネントの型名になります。");
        UI::Separator();

        UI::InputText("置き先", newScriptFolder_, sizeof(newScriptFolder_));
        UI::InputText("クラス名", newScriptName_, sizeof(newScriptName_));

        const std::vector<Editor::ScriptTemplate::Entry> templates = Editor::ScriptTemplate::List();
        if (templates.empty()) {
            UI::Hint("雛形が見つかりません（Application/Config/ScriptTemplates）。中身の無いクラスを作ります。");
        } else {
            ImGui::TextUnformatted("雛形");
            for (const auto& entry : templates) {
                ImGui::PushID(entry.id.c_str());
                if (ImGui::RadioButton(entry.label.c_str(), newScriptTemplate_ == entry.id)) {
                    newScriptTemplate_ = entry.id;
                }
                ImGui::PopID();
            }
        }

        ImGui::Checkbox("作ったら VS Code で開く", &openNewScriptAfterCreate_);

        if (!newScriptError_.empty()) {
            ImGui::TextColored(Theme::kError, "%s", newScriptError_.c_str());
        }

        UI::Separator();
        if (ImGui::Button("作成")) {
            std::filesystem::path created;
            if (Editor::ScriptTemplate::Create(std::filesystem::path(newScriptFolder_),
                    newScriptName_, newScriptTemplate_, &created, &newScriptError_)) {
                showNewScriptDialog_ = false;
                ImGui::CloseCurrentPopup();
                // 作った場所を開いて、そのファイルを選んでおく
                NavigateToDirectory(created.parent_path());
                selectedPath_ = created;
                if (openNewScriptAfterCreate_) {
                    Editor::OpenInCodeEditor(created);
                }
            }
        }
        UI::SameLine();
        if (ImGui::Button("やめる")) {
            showNewScriptDialog_ = false;
            newScriptError_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectView::OpenFile(const std::filesystem::path& filePath)
    {
        if (!std::filesystem::exists(filePath)) {
            return;
        }

        HINSTANCE result = ::ShellExecuteW(nullptr, L"open", filePath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}", "Failed to open file: " + filePath.string());
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
#endif // CORE_EDITOR


