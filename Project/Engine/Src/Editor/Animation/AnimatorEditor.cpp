#include "pch.h"
#include "AnimatorEditor.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorLiveView.h"

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Model/ModelManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <imgui.h>

#include <algorithm>
#include <system_error>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr float kLeftWidth = 270.0f;
        constexpr float kRightWidth = 330.0f;

        constexpr const char* kCloseDialog = "保存していない変更##AnimatorClose";

        /// @brief 設定の変更 1 回（前後の中身を JSON の文で持つ）
        class ControllerEditCommand final : public IEditorCommand
        {
        public:
            ControllerEditCommand(std::string label, const void* document, std::string before, std::string after)
                : label_(std::move(label)), document_(document), before_(std::move(before)), after_(std::move(after)) {}

            void Undo() override { AnimatorEditor::Get().RestoreDocument(document_, before_); }
            void Redo() override { AnimatorEditor::Get().RestoreDocument(document_, after_); }
            std::string GetLabel() const override { return label_; }
            bool References(const void* target) const override { return target != nullptr && target == document_; }
            bool AffectsScene() const override { return false; }
            bool SurvivesSceneReload() const override { return true; }

        private:
            std::string label_;
            const void* document_ = nullptr;
            std::string before_;
            std::string after_;
        };
    }

    AnimatorEditor::AnimatorEditor() = default;
    AnimatorEditor::~AnimatorEditor() = default;

    AnimatorEditor& AnimatorEditor::Get()
    {
        static AnimatorEditor instance;
        return instance;
    }

    void AnimatorEditor::Initialize(EngineSystem* engine)
    {
        engine_ = engine;
    }

    void AnimatorEditor::Finalize()
    {
        for (const std::unique_ptr<AnimatorDocument>& document : documents_) {
            EditorCommandStack::Get().RemoveCommandsReferencing(document.get());
        }
        preview_.Forget();
        documents_.clear();
        activeDocument_ = 0;
        closingDocument_.reset();
        engine_ = nullptr;
    }

    AnimatorDocument* AnimatorEditor::FindDocument(const void* document)
    {
        for (const std::unique_ptr<AnimatorDocument>& candidate : documents_) {
            if (candidate.get() == document) {
                return candidate.get();
            }
        }
        return nullptr;
    }

    bool AnimatorEditor::Open(const std::filesystem::path& file)
    {
        for (const std::unique_ptr<AnimatorDocument>& document : documents_) {
            std::error_code ec;
            if (document->file == file || std::filesystem::equivalent(document->file, file, ec)) {
                document->selectOnNextFrame = true;
                focusRequested_ = true;
                if (EditorPanel* const panel = EditorPanelRegistry::Get().Find(kPanelId)) {
                    panel->visible = true;
                }
                return true;
            }
        }

        ModelManager* const models = engine_ ? engine_->GetService<ModelManager>() : nullptr;
        std::unique_ptr<AnimatorDocument> document = AnimatorDocument::Load(file, models);
        if (!document) {
            return false;
        }
        documents_.push_back(std::move(document));

        focusRequested_ = true;
        if (EditorPanel* const panel = EditorPanelRegistry::Get().Find(kPanelId)) {
            panel->visible = true;
        }
        return true;
    }

    bool AnimatorEditor::HasUnsavedChanges() const
    {
        return std::any_of(documents_.begin(), documents_.end(),
            [](const std::unique_ptr<AnimatorDocument>& document) { return document->IsDirty(); });
    }

    void AnimatorEditor::RestoreDocument(const void* target, const std::string& text)
    {
        if (AnimatorDocument* const document = FindDocument(target)) {
            document->Restore(text);
        }
    }

    bool AnimatorEditor::CreateControllerFile(const std::filesystem::path& folder, const std::string& name,
        std::filesystem::path* created, std::string* error)
    {
        const auto fail = [error](const char* message) {
            if (error) {
                *error = message;
            }
            return false;
            };

        const size_t first = name.find_first_not_of(" \t");
        const size_t last = name.find_last_not_of(" \t");
        const std::string trimmed = first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
        if (trimmed.empty()) {
            return fail("名前を入れてください。");
        }
        if (trimmed.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            return fail("名前に使えない文字があります（\\ / : * ? \" < > |）。");
        }

        Logger& log = Logger::GetInstance();
        const std::filesystem::path directory = folder.is_absolute() ? folder : ProjectPaths::Resolve(log.PathToUtf8(folder));
        if (directory.empty()) {
            return fail("置くフォルダがありません。");
        }
        const std::filesystem::path file = directory / log.Utf8ToPath(trimmed + AnimatorControllerFormat::kExtension);
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            return fail("同じ名前のファイルがあります。");
        }
        if (!AnimatorDocument::WriteText(file, AnimatorDocument::ToText(AnimatorControllerFormat::MakeEmpty()))) {
            return fail("ファイルを書けません。");
        }
        AssetDatabase::GetInstance().ImportAsset(file);
        if (created) {
            *created = file;
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    // ---------------------------------------------------------------- 窓

    void AnimatorEditor::Draw()
    {
        if (!engine_) {
            return;
        }
        if (focusRequested_) {
            ImGui::SetWindowFocus();
            focusRequested_ = false;
        }
        if (documents_.empty()) {
            preview_.Stop();
            UI::Hint("Project で .animator をダブルクリックすると、ここで開きます。");
            UI::Hint("新しい設定は Project の右クリック > アニメーター設定を作成 で作れます。");
            UI::Hint("作った設定を Animator の「設定」の欄へドラッグすると、その設定で動きます。");
            return;
        }

        std::optional<size_t> closeNow;
        bool openCloseDialog = false;
        if (ImGui::BeginTabBar("##AnimatorTabs", ImGuiTabBarFlags_Reorderable)) {
            for (size_t index = 0; index < documents_.size(); ++index) {
                AnimatorDocument& document = *documents_[index];
                const bool dirty = document.IsDirty();
                ImGuiTabItemFlags flags = dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                if (document.selectOnNextFrame) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    document.selectOnNextFrame = false;
                }
                bool open = true;
                const std::string label = document.title + "###" + Logger::GetInstance().PathToUtf8(document.file);
                if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                    activeDocument_ = index;
                    DrawDocument(document);
                    ImGui::EndTabItem();
                }
                if (!open) {
                    if (dirty) {
                        closingDocument_ = index;
                        openCloseDialog = true;
                    } else {
                        closeNow = index;
                    }
                }
            }
            ImGui::EndTabBar();
        }
        // 確認の窓は DrawCloseDialog と同じ ID の並び（タブバーの外）で開く
        if (openCloseDialog) {
            ImGui::OpenPopup(kCloseDialog);
        }
        DrawCloseDialog();
        if (closeNow) {
            CloseDocument(*closeNow);
        }
    }

    void AnimatorEditor::DrawCloseDialog()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(kCloseDialog, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        AnimatorDocument* const document = closingDocument_ && *closingDocument_ < documents_.size()
            ? documents_[*closingDocument_].get() : nullptr;
        if (!document) {
            closingDocument_.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("「%s」に保存していない変更があります。", document->title.c_str());
        ImGui::Spacing();
        std::optional<size_t> close;
        if (ImGui::Button("保存して閉じる")) {
            document->Save();
            if (!document->IsDirty()) {
                close = closingDocument_;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存せずに閉じる")) {
            close = closingDocument_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる")) {
            ImGui::CloseCurrentPopup();
            closingDocument_.reset();
        }
        ImGui::EndPopup();
        if (close) {
            closingDocument_.reset();
            CloseDocument(*close);
        }
    }

    void AnimatorEditor::CloseDocument(size_t index)
    {
        if (index >= documents_.size()) {
            return;
        }
        preview_.Stop();
        EditorCommandStack::Get().RemoveCommandsReferencing(documents_[index].get());
        documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
        if (activeDocument_ >= documents_.size()) {
            activeDocument_ = documents_.empty() ? 0 : documents_.size() - 1;
        }
    }

    void AnimatorEditor::DrawDocument(AnimatorDocument& document)
    {
        if (document.data.layers.empty()) {
            document.data.layers.emplace_back();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
            document.Save();
        }

        const AnimatorLiveView live = AnimatorLiveView::Find(document);
        preview_.Update(document);
        DrawToolbar(document, live);

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float footer = document.notice.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const float height = std::max(120.0f, available.y - footer);
        const float canvasWidth = std::max(160.0f, available.x - kLeftWidth - kRightWidth - style.ItemSpacing.x * 2.0f);

        ImGui::BeginChild("##AnimatorLeft", ImVec2(kLeftWidth, height), ImGuiChildFlags_Borders);
        layerPanel_.DrawList(document, live);
        parameterPanel_.Draw(document, live);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##AnimatorCanvas", ImVec2(canvasWidth, height), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        canvas_.Draw(document, live);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##AnimatorInspector", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
        if (document.selection == AnimatorSelection::Layer) {
            layerPanel_.DrawInspector(document, live);
        } else {
            inspectorPanel_.Draw(document, live);
        }
        ImGui::EndChild();

        if (!document.notice.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextWrapped("%s", document.notice.c_str());
            ImGui::PopStyleColor();
        }

        CommitIfChanged(document);
    }

    void AnimatorEditor::DrawToolbar(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        if (ImGui::Button("保存")) {
            document.Save();
        }
        UI::Tooltip("Ctrl+S。保存すると、再生中のオブジェクトにもすぐ効きます。");
        ImGui::SameLine();
        if (ImGui::Button("全体を見る")) {
            document.frameRequested = true;
        }
        ImGui::SameLine();
        DrawPreviewControls(document);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (document.previewEnabled && !PlaybackStateManager::GetInstance().IsInPlayMode()) {
            const std::string state = AnimatorPreview::PreviewStateOf(document);
            if (document.previewObject.empty()) {
                UI::Hint("Hierarchy で、この設定の Animator を付けたオブジェクトを選んでください。");
            } else {
                ImGui::TextColored(Theme::kWarm, "シーンで見る: %s（%s）", document.previewObject.c_str(), state.c_str());
            }
        } else if (live.runtime) {
            ImGui::TextColored(Theme::kOk, "再生中: %s（%s）", live.objectName.c_str(),
                live.currentState.empty() ? "状態なし" : live.currentState.c_str());
        } else if (PlaybackStateManager::GetInstance().IsInPlayMode()) {
            UI::Hint("Hierarchy でこの設定を使うオブジェクトを選ぶと、今の状態が映ります。");
        } else {
            UI::Hint("右クリックで状態を作り、状態の右クリック > 遷移を引く でつなぎます。");
        }
    }

    void AnimatorEditor::DrawPreviewControls(AnimatorDocument& document)
    {
        const bool playMode = PlaybackStateManager::GetInstance().IsInPlayMode();
        ImGui::BeginDisabled(playMode);
        ImGui::Checkbox("シーンで見る", &document.previewEnabled);
        ImGui::EndDisabled();
        UI::Tooltip("停止中に、Hierarchy で選んだオブジェクト（この設定の Animator を付けたもの）へ、選んだ状態の姿勢を出します。"
            "保存する前の中身で動きます。");
        if (!document.previewEnabled || playMode) {
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button(document.previewPlaying ? "止める##preview" : "流す##preview")) {
            document.previewPlaying = !document.previewPlaying;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("##previewTime", &document.previewTime, 0.0f, 1.0f, "進み %.2f")) {
            document.previewPlaying = false;
        }
    }

    void AnimatorEditor::CommitIfChanged(AnimatorDocument& document)
    {
        if (document.pendingLabel.empty() || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            return;
        }
        document.currentText = AnimatorDocument::ToText(document.data);
        if (document.currentText != document.committedText) {
            EditorCommandStack::Get().Push(std::make_unique<ControllerEditCommand>("アニメーター: " + document.pendingLabel,
                &document, document.committedText, document.currentText));
            document.committedText = document.currentText;
        }
        document.pendingLabel.clear();
    }
}

#endif // CORE_EDITOR
