#include "pch.h"
#include "ShaderGraphEditor.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Panel/EditorMenuRegistry.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/ShaderGraph/ShaderGraphAsset.h"
#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"
#include "Editor/ShaderGraph/ShaderGraphSelfTest.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief ピンの番号の作り方（ノードの番号 × 64 ＋ 入力は 0〜31・出力は 32〜63）
        constexpr int kPinStride = 64;
        constexpr int kOutputOffset = 32;

        /// @brief 最後に変えてから検査コンパイルを始めるまでの秒数
        constexpr double kValidateDelay = 0.3;

        constexpr float kBlackboardWidth = 220.0f;
        constexpr float kSideWidth = 280.0f;
        constexpr float kMessagesHeight = 96.0f;
        constexpr float kInlineFieldWidth = 56.0f;
        constexpr float kLabelWidth = 84.0f;

        constexpr const char* kPropertyPayload = "SHADERGRAPH_PROPERTY";
        constexpr const char* kTexturePayload = "TEXTURE_FILE";
        constexpr const char* kSearchPopup = "##ShaderGraphSearch";
        constexpr const char* kNodeMenuPopup = "##ShaderGraphNodeMenu";
        constexpr const char* kAddPropertyPopup = "##ShaderGraphAddProperty";
        constexpr const char* kCloseDialog = "シェーダーグラフを閉じる";

        int InputPinId(int node, size_t index)
        {
            return node * kPinStride + static_cast<int>(index);
        }

        int OutputPinId(int node, size_t index)
        {
            return node * kPinStride + kOutputOffset + static_cast<int>(index);
        }

        /// @brief ピンの番号を読み解いたもの
        struct PinRef
        {
            int node = 0;
            bool output = false;
            size_t index = 0;
        };

        PinRef DecodePin(int id)
        {
            PinRef pin;
            pin.node = id / kPinStride;
            const int local = id % kPinStride;
            pin.output = local >= kOutputOffset;
            pin.index = static_cast<size_t>(pin.output ? local - kOutputOffset : local);
            return pin;
        }

        ImU32 ToU32(const ImVec4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        ImVec4 Lighten(const ImVec4& color, float amount)
        {
            return ImVec4(color.x + (1.0f - color.x) * amount, color.y + (1.0f - color.y) * amount,
                color.z + (1.0f - color.z) * amount, color.w);
        }

        /// @brief 値の種類ごとのピンとつながりの色
        ImVec4 PinColor(ValueType type)
        {
            switch (type) {
            case ValueType::Float: return Theme::FromSrgb(132, 228, 231);
            case ValueType::Vector2: return Theme::FromSrgb(154, 239, 146);
            case ValueType::Vector3: return Theme::FromSrgb(246, 255, 154);
            case ValueType::Vector4: return Theme::FromSrgb(251, 203, 244);
            case ValueType::Texture2D: return Theme::FromSrgb(255, 139, 139);
            case ValueType::SamplerState: return Theme::FromSrgb(170, 170, 178);
            default: return Theme::kTextDim;
            }
        }

        ImNodesPinShape PinShape(ValueType type, bool connected)
        {
            if (type == ValueType::Texture2D) {
                return connected ? ImNodesPinShape_QuadFilled : ImNodesPinShape_Quad;
            }
            if (type == ValueType::SamplerState) {
                return connected ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_Triangle;
            }
            return connected ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
        }

        const char* ValueTypeName(ValueType type)
        {
            switch (type) {
            case ValueType::Float: return "Float";
            case ValueType::Vector2: return "Vector2";
            case ValueType::Vector3: return "Vector3";
            case ValueType::Vector4: return "Vector4";
            case ValueType::Dynamic: return "Dynamic";
            case ValueType::Texture2D: return "Texture2D";
            case ValueType::SamplerState: return "SamplerState";
            }
            return "?";
        }

        /// @brief from の出力を to の入力へつなげるか（数どうしは幅を直してつなぐ）
        bool CanConnect(ValueType from, ValueType to)
        {
            if (IsNumeric(from) && IsNumeric(to)) {
                return true;
            }
            return from == to;
        }

        constexpr PropertyType kPropertyTypes[] = {
            PropertyType::Float, PropertyType::Vector2, PropertyType::Vector3,
            PropertyType::Vector4, PropertyType::Color, PropertyType::Texture2D,
        };

        const char* PropertyTypeLabel(PropertyType type)
        {
            switch (type) {
            case PropertyType::Float: return "Float";
            case PropertyType::Vector2: return "Vector 2";
            case PropertyType::Vector3: return "Vector 3";
            case PropertyType::Vector4: return "Vector 4";
            case PropertyType::Color: return "Color";
            case PropertyType::Texture2D: return "Texture 2D";
            }
            return "?";
        }

        /// @brief 新しいプロパティの名前の初め
        const char* PropertyBaseName(PropertyType type)
        {
            switch (type) {
            case PropertyType::Float: return "NewFloat";
            case PropertyType::Vector2: return "NewVector2";
            case PropertyType::Vector3: return "NewVector3";
            case PropertyType::Vector4: return "NewVector4";
            case PropertyType::Color: return "NewColor";
            case PropertyType::Texture2D: return "NewTexture";
            }
            return "NewProperty";
        }

        std::string Lower(std::string_view text)
        {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            return result;
        }

        bool Contains(std::string_view text, const std::string& lowerFilter)
        {
            return Lower(text).find(lowerFilter) != std::string::npos;
        }

        /// @brief つないでいない入力の値（書いていなければピンの既定）
        std::array<float, 4> InputValue(const Node& node, const PinDesc& pin)
        {
            if (const auto found = node.values.find(pin.name); found != node.values.end()) {
                return found->second;
            }
            return pin.fallback;
        }

        /// @brief 設定の値（書いていなければ既定）
        std::string SettingValue(const Node& node, const SettingDesc& setting)
        {
            if (const auto found = node.settings.find(setting.name); found != node.settings.end()) {
                return found->second;
            }
            return std::string(setting.fallback);
        }

        /// @brief つないでいないときに使うものの説明（ノードに書いた値を使うなら nullptr）
        const char* BindingHint(PinBinding binding)
        {
            switch (binding) {
            case PinBinding::UV: return "UV0";
            case PinBinding::MainTexture: return "Main";
            case PinBinding::Sampler: return "既定";
            case PinBinding::SpriteRGB:
            case PinBinding::SpriteAlpha: return "既定の色";
            default: return nullptr;
            }
        }

        std::string GraphText(const Graph& graph)
        {
            return WriteGraph(graph).dump();
        }

        /// @brief 欄の名前を左に書き、欄を残りの幅いっぱいにする
        void Row(const char* label)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(kLabelWidth);
            ImGui::SetNextItemWidth(-FLT_MIN);
        }

        /// @brief テクスチャのパスの欄（書くか、Project のテクスチャを落とす）
        /// @return 変えたら true
        bool TexturePathField(const char* id, std::string& path)
        {
            bool changed = false;
            std::string text = path;
            if (UI::InputStringWithHint(id, "（白）", text, ImGuiInputTextFlags_EnterReturnsTrue)) {
                path = text;
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTexturePayload)) {
                    const auto* data = static_cast<const char*>(payload->Data);
                    const std::string fileName(data, strnlen(data, static_cast<size_t>(payload->DataSize)));
                    if (const AssetInfo* const dropped = FindAssetInfo(fileName, AssetType::Texture)) {
                        path = ToAssetPath(*dropped);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            return changed;
        }

        /// @brief 呼び出しの間だけ、シェーダーグラフの imnodes の状態へ切り替える
        class NodesScope
        {
        public:
            NodesScope(ImNodesContext* context, ImNodesEditorContext* editor)
                : previous_(ImNodes::GetCurrentContext())
            {
                ImNodes::SetCurrentContext(context);
                if (editor) {
                    ImNodes::EditorContextSet(editor);
                }
            }

            ~NodesScope()
            {
                ImNodes::SetCurrentContext(previous_);
            }

            NodesScope(const NodesScope&) = delete;
            NodesScope& operator=(const NodesScope&) = delete;

        private:
            ImNodesContext* previous_ = nullptr;
        };

        /// @brief グラフの変更 1 回（前後の中身を JSON の文で持つ）
        class GraphEditCommand final : public IEditorCommand
        {
        public:
            GraphEditCommand(std::string label, const void* document, std::string before, std::string after)
                : label_(std::move(label)), document_(document), before_(std::move(before)), after_(std::move(after)) {}

            void Undo() override { ShaderGraphEditor::Get().RestoreDocument(document_, before_); }
            void Redo() override { ShaderGraphEditor::Get().RestoreDocument(document_, after_); }
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

    ShaderGraphEditor& ShaderGraphEditor::Get()
    {
        static ShaderGraphEditor instance;
        return instance;
    }

    void ShaderGraphEditor::Initialize(EngineSystem* engine)
    {
        engine_ = engine;
        compiler_ = std::make_unique<ShaderCompiler>();
        compiler_->Initialize();
        preview_.Initialize(engine);
        selfTestMenu_ = EditorMenuRegistry::Get().Register({
            .path = "Tools/テスト/シェーダーグラフの全ノード検査",
            .action = [this] { RunSelfTest(); },
            });
    }

    void ShaderGraphEditor::Finalize()
    {
        selfTestMenu_.Reset();
        if (selfTest_.joinable()) {
            selfTest_.request_stop();
            selfTest_.join();
        }
        for (const std::unique_ptr<Document>& document : documents_) {
            EditorCommandStack::Get().RemoveCommandsReferencing(document.get());
            if (document->context) {
                ImNodes::EditorContextFree(document->context);
            }
        }
        documents_.clear();
        if (nodesContext_) {
            ImNodes::DestroyContext(nodesContext_);
            nodesContext_ = nullptr;
        }
        preview_.Finalize();
        compiler_.reset();
        engine_ = nullptr;
    }

    void ShaderGraphEditor::EnsureNodesContext()
    {
        if (nodesContext_) {
            return;
        }
        ImNodesContext* const previous = ImNodes::GetCurrentContext();
        nodesContext_ = ImNodes::CreateContext();
        ImNodes::SetCurrentContext(nodesContext_);

        ImNodesStyle& style = ImNodes::GetStyle();
        style.NodeCornerRounding = 5.0f;
        style.NodePadding = ImVec2(8.0f, 6.0f);
        style.LinkThickness = 2.4f;
        style.PinCircleRadius = 4.0f;
        style.PinQuadSideLength = 8.0f;
        style.PinTriangleSideLength = 10.0f;
        style.GridSpacing = 32.0f;
        style.Flags = ImNodesStyleFlags_NodeOutline | ImNodesStyleFlags_GridLines | ImNodesStyleFlags_GridLinesPrimary;

        unsigned int* const colors = style.Colors;
        colors[ImNodesCol_NodeBackground] = ToU32(Theme::kWindow);
        colors[ImNodesCol_NodeBackgroundHovered] = ToU32(Theme::kPanel);
        colors[ImNodesCol_NodeBackgroundSelected] = ToU32(Theme::kPanel);
        colors[ImNodesCol_NodeOutline] = ToU32(Theme::kOutline);
        colors[ImNodesCol_TitleBar] = ToU32(Theme::kControl);
        colors[ImNodesCol_TitleBarHovered] = ToU32(Theme::kHover);
        colors[ImNodesCol_TitleBarSelected] = ToU32(Theme::kAccent);
        colors[ImNodesCol_Link] = ToU32(Theme::kAccent);
        colors[ImNodesCol_LinkHovered] = ToU32(Theme::kAccentHover);
        colors[ImNodesCol_LinkSelected] = ToU32(Theme::kWarm);
        colors[ImNodesCol_Pin] = ToU32(Theme::kTextDim);
        colors[ImNodesCol_PinHovered] = ToU32(Theme::kAccentHover);
        colors[ImNodesCol_BoxSelector] = ToU32(Theme::WithAlpha(Theme::kAccent, 0.2f));
        colors[ImNodesCol_BoxSelectorOutline] = ToU32(Theme::kAccent);
        colors[ImNodesCol_GridBackground] = ToU32(Theme::kDeepest);
        colors[ImNodesCol_GridLine] = ToU32(Theme::WithAlpha(Theme::kOutline, 0.45f));
        colors[ImNodesCol_GridLinePrimary] = ToU32(Theme::kOutline);
        colors[ImNodesCol_MiniMapBackground] = ToU32(Theme::WithAlpha(Theme::kChild, 0.9f));
        colors[ImNodesCol_MiniMapBackgroundHovered] = ToU32(Theme::WithAlpha(Theme::kChild, 0.95f));
        colors[ImNodesCol_MiniMapOutline] = ToU32(Theme::kOutline);
        colors[ImNodesCol_MiniMapOutlineHovered] = ToU32(Theme::kAccent);
        colors[ImNodesCol_MiniMapNodeBackground] = ToU32(Theme::kControl);
        colors[ImNodesCol_MiniMapNodeBackgroundHovered] = ToU32(Theme::kHover);
        colors[ImNodesCol_MiniMapNodeBackgroundSelected] = ToU32(Theme::kAccent);
        colors[ImNodesCol_MiniMapNodeOutline] = ToU32(Theme::kOutline);
        colors[ImNodesCol_MiniMapLink] = ToU32(Theme::kAccent);
        colors[ImNodesCol_MiniMapLinkSelected] = ToU32(Theme::kWarm);
        colors[ImNodesCol_MiniMapCanvas] = ToU32(Theme::WithAlpha(Theme::kText, 0.08f));
        colors[ImNodesCol_MiniMapCanvasOutline] = ToU32(Theme::kTextDim);

        // Alt + 左ドラッグでも表示を動かせるようにする
        ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
        ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyCtrl;

        // つながっている入力からドラッグすると、つながりを外せるようにする
        ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);

        ImNodes::SetCurrentContext(previous);
    }

    ShaderGraphEditor::Document* ShaderGraphEditor::FindDocument(const void* document)
    {
        for (const std::unique_ptr<Document>& candidate : documents_) {
            if (candidate.get() == document) {
                return candidate.get();
            }
        }
        return nullptr;
    }

    ShaderGraphEditor::Document* ShaderGraphEditor::ActiveDocument()
    {
        return activeDocument_ < documents_.size() ? documents_[activeDocument_].get() : nullptr;
    }

    bool ShaderGraphEditor::Open(const std::filesystem::path& graphFile)
    {
        Logger& log = Logger::GetInstance();
        for (const std::unique_ptr<Document>& document : documents_) {
            std::error_code ec;
            if (document->file == graphFile || std::filesystem::equivalent(document->file, graphFile, ec)) {
                document->selectOnNextFrame = true;
                focusRequested_ = true;
                if (EditorPanel* const panel = EditorPanelRegistry::Get().Find(kPanelId)) {
                    panel->visible = true;
                }
                return true;
            }
        }

        Graph graph;
        std::string error;
        std::vector<std::string> warnings;
        if (!LoadGraphFile(graphFile, graph, error, warnings)) {
            log.Log("シェーダーグラフを開けません: " + log.PathToUtf8(graphFile) + "（" + error + "）",
                LogLevel::Warn, LogCategory::Shader);
            return false;
        }

        auto document = std::make_unique<Document>();
        document->file = graphFile;
        document->title = log.PathToUtf8(graphFile.stem());
        document->graph = std::move(graph);
        document->currentText = GraphText(document->graph);
        document->savedText = document->currentText;
        document->committedText = document->currentText;
        for (const std::string& warning : warnings) {
            document->notice += (document->notice.empty() ? "" : " / ") + warning;
        }
        document->context = ImNodes::EditorContextCreate();
        document->selectOnNextFrame = true;
        documents_.push_back(std::move(document));

        focusRequested_ = true;
        if (EditorPanel* const panel = EditorPanelRegistry::Get().Find(kPanelId)) {
            panel->visible = true;
        }
        return true;
    }

    bool ShaderGraphEditor::HasUnsavedChanges() const
    {
        return std::any_of(documents_.begin(), documents_.end(),
            [](const std::unique_ptr<Document>& document) { return document->currentText != document->savedText; });
    }

    void ShaderGraphEditor::RestoreDocument(const void* target, const std::string& text)
    {
        Document* const document = FindDocument(target);
        if (!document) {
            return;
        }
        const json root = json::parse(text, nullptr, false);
        Graph graph;
        std::vector<std::string> warnings;
        if (root.is_discarded() || !ReadGraph(root, graph, warnings)) {
            return;
        }
        document->graph = std::move(graph);
        document->currentText = GraphText(document->graph);
        document->committedText = document->currentText;
        document->pendingLabel.clear();
        document->syncPositions = true;
        document->clearSelection = true;
        document->selectedNodes.clear();
        document->selectedLinks.clear();
        if (!document->graph.FindProperty(document->selectedProperty)) {
            document->selectedProperty.clear();
        }
    }

    void ShaderGraphEditor::RecordPreview(ID3D12GraphicsCommandList* commandList)
    {
        preview_.Record(commandList);
    }

    // ---------------------------------------------------------------- 窓

    void ShaderGraphEditor::Draw()
    {
        if (!engine_) {
            return;
        }
        if (focusRequested_) {
            ImGui::SetWindowFocus();
            focusRequested_ = false;
        }
        if (documents_.empty()) {
            UI::Hint("Project で .shadergraph をダブルクリックすると、ここで開きます。");
            UI::Hint("新しいグラフは Project の右クリック > シェーダーグラフを作成 で作れます。");
            return;
        }

        std::optional<size_t> closeNow;
        if (ImGui::BeginTabBar("##ShaderGraphTabs", ImGuiTabBarFlags_Reorderable)) {
            for (size_t index = 0; index < documents_.size(); ++index) {
                Document& document = *documents_[index];
                const bool dirty = document.currentText != document.savedText;
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
                        ImGui::OpenPopup(kCloseDialog);
                    } else {
                        closeNow = index;
                    }
                }
            }
            ImGui::EndTabBar();
        }
        DrawCloseDialog();
        if (closeNow) {
            CloseDocument(*closeNow);
        }
    }

    void ShaderGraphEditor::DrawCloseDialog()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(kCloseDialog, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        Document* const document = closingDocument_ && *closingDocument_ < documents_.size()
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
            Save(*document);
            if (document->currentText == document->savedText) {
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

    void ShaderGraphEditor::CloseDocument(size_t index)
    {
        if (index >= documents_.size()) {
            return;
        }
        Document* const document = documents_[index].get();
        EditorCommandStack::Get().RemoveCommandsReferencing(document);
        if (document->context) {
            ImNodes::EditorContextFree(document->context);
        }
        documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
        if (activeDocument_ >= documents_.size()) {
            activeDocument_ = documents_.empty() ? 0 : documents_.size() - 1;
        }
    }

    void ShaderGraphEditor::DrawDocument(Document& document)
    {
        document.resolution = Resolve(document.graph);

        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) {
            Save(document);
        }
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
                CopySelection(document);
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) {
                if (document.canvasHovered) {
                    Paste(document, document.mouseGridX, document.mouseGridY);
                } else {
                    Paste(document, document.viewCenterX, document.viewCenterY);
                }
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && !document.selectedNodes.empty()) {
                float minX = FLT_MAX;
                float minY = FLT_MAX;
                for (const int id : document.selectedNodes) {
                    if (const Node* const node = document.graph.FindNode(id)) {
                        minX = (std::min)(minX, node->x);
                        minY = (std::min)(minY, node->y);
                    }
                }
                CopySelection(document);
                if (minX != FLT_MAX) {
                    Paste(document, minX + 40.0f, minY + 40.0f);
                }
            }
        }

        DrawToolbar(document);

        const bool hasMessages = !document.error.empty() || !document.issues.empty() || !document.notice.empty();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float height = (std::max)(120.0f, available.y - (hasMessages ? kMessagesHeight + ImGui::GetStyle().ItemSpacing.y : 0.0f));
        const float canvasWidth = (std::max)(160.0f, available.x - kBlackboardWidth - kSideWidth - spacing * 2.0f);

        if (ImGui::BeginChild("##Blackboard", ImVec2(kBlackboardWidth, height), ImGuiChildFlags_Borders)) {
            DrawBlackboard(document);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##Canvas", ImVec2(canvasWidth, height), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            DrawCanvas(document);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##Side", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
            DrawPreview(document);
            ImGui::Separator();
            DrawInspector(document);
        }
        ImGui::EndChild();
        if (hasMessages) {
            if (ImGui::BeginChild("##Messages", ImVec2(0.0f, kMessagesHeight), ImGuiChildFlags_Borders)) {
                DrawMessages(document);
            }
            ImGui::EndChild();
        }

        CommitIfChanged(document);
        UpdateValidation(document);
        if (document.previewRevision > 0) {
            RebuildPreviewValues(document);
            preview_.Request(ScratchShaderPath(ScratchKey(document) + ".preview"), document.previewRevision,
                document.previewValues, document.previewTexture);
        }
    }

    void ShaderGraphEditor::DrawToolbar(Document& document)
    {
        if (ImGui::Button("保存")) {
            Save(document);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Ctrl+S。グラフを書き、シェーダを作り直します。");
        }
        ImGui::SameLine();
        if (document.currentText != document.savedText) {
            ImGui::TextColored(Theme::kWarm, "未保存の変更");
        } else {
            ImGui::TextDisabled("保存済み");
        }
        ImGui::SameLine(0.0f, 16.0f);
        if (!document.error.empty()) {
            ImGui::TextColored(Theme::kError, "シェーダにエラー");
        } else if (document.validatePending) {
            ImGui::TextDisabled("確かめています…");
        } else if (!document.validatedHlsl.empty()) {
            ImGui::TextColored(Theme::kOk, "シェーダ OK");
        }
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("%s", ToAssetText(document.file).c_str());
    }

    // ---------------------------------------------------------------- キャンバス

    void ShaderGraphEditor::DrawCanvas(Document& document)
    {
        EnsureNodesContext();
        NodesScope scope(nodesContext_, document.context);

        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        document.canvasWidth = size.x;
        document.canvasHeight = size.y;

        ImNodes::BeginNodeEditor();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        std::vector<int> drawn;
        drawn.reserve(document.graph.nodes.size());
        for (Node& node : document.graph.nodes) {
            if (document.syncPositions || document.placeNodes.contains(node.id)) {
                ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.x, node.y));
            }
            DrawNode(document, node);
            drawn.push_back(node.id);
        }

        for (const Link& link : document.graph.links) {
            const Node* const from = document.graph.FindNode(link.fromNode);
            const Node* const to = document.graph.FindNode(link.toNode);
            const NodeDesc* const fromDesc = from ? FindNodeDesc(from->type) : nullptr;
            const NodeDesc* const toDesc = to ? FindNodeDesc(to->type) : nullptr;
            if (!fromDesc || !toDesc) {
                continue;
            }
            const int output = fromDesc->FindOutput(link.fromPin);
            const int input = toDesc->FindInput(link.toPin);
            if (output < 0 || input < 0) {
                continue;
            }
            ValueType type = fromDesc->outputs[static_cast<size_t>(output)].type;
            if (const auto resolved = document.resolution.nodes.find(link.fromNode); resolved != document.resolution.nodes.end()
                && static_cast<size_t>(output) < resolved->second.outputs.size()) {
                type = resolved->second.outputs[static_cast<size_t>(output)];
            }
            const ImVec4 color = PinColor(type);
            ImNodes::PushColorStyle(ImNodesCol_Link, ToU32(color));
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, ToU32(Lighten(color, 0.35f)));
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, ToU32(Theme::kWarm));
            const int inputPin = InputPinId(link.toNode, static_cast<size_t>(input));
            ImNodes::Link(inputPin, OutputPinId(link.fromNode, static_cast<size_t>(output)), inputPin);
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }

        // 選んだノード（無ければ全部）を表示の真ん中へ寄せる
        if (document.frameRequested && !drawn.empty()) {
            const std::vector<int>& targets = document.selectedNodes.empty() ? drawn : document.selectedNodes;
            ImVec2 boundsMin(FLT_MAX, FLT_MAX);
            ImVec2 boundsMax(-FLT_MAX, -FLT_MAX);
            for (const int id : targets) {
                if (std::find(drawn.begin(), drawn.end(), id) == drawn.end()) {
                    continue;
                }
                const ImVec2 position = ImNodes::GetNodeGridSpacePos(id);
                const ImVec2 dimensions = ImNodes::GetNodeDimensions(id);
                boundsMin = ImVec2((std::min)(boundsMin.x, position.x), (std::min)(boundsMin.y, position.y));
                boundsMax = ImVec2((std::max)(boundsMax.x, position.x + dimensions.x), (std::max)(boundsMax.y, position.y + dimensions.y));
            }
            if (boundsMin.x <= boundsMax.x) {
                ImNodes::EditorContextResetPanning(ImVec2(
                    size.x * 0.5f - (boundsMin.x + boundsMax.x) * 0.5f,
                    size.y * 0.5f - (boundsMin.y + boundsMax.y) * 0.5f));
            }
        }
        document.frameRequested = false;

        ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
        const bool editorHovered = ImNodes::IsEditorHovered();
        ImNodes::EndNodeEditor();

        // ドラッグで動いた位置をグラフへ戻す
        bool moved = false;
        for (const int id : drawn) {
            Node* const node = document.graph.FindNode(id);
            if (!node) {
                continue;
            }
            const ImVec2 position = ImNodes::GetNodeGridSpacePos(id);
            if (std::abs(position.x - node->x) > 0.5f || std::abs(position.y - node->y) > 0.5f) {
                moved = true;
            }
            node->x = position.x;
            node->y = position.y;
            document.placeNodes.erase(id);
        }
        if (moved && document.pendingLabel.empty() && !document.syncPositions) {
            document.pendingLabel = "ノードを動かす";
        }
        document.syncPositions = false;

        const ImVec2 panning = ImNodes::EditorContextGetPanning();
        const ImVec2 mouse = ImGui::GetMousePos();
        document.mouseGridX = mouse.x - origin.x - panning.x;
        document.mouseGridY = mouse.y - origin.y - panning.y;
        document.viewCenterX = size.x * 0.5f - panning.x;
        document.viewCenterY = size.y * 0.5f - panning.y;
        document.canvasHovered = editorHovered;

        if (document.clearSelection) {
            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
            document.clearSelection = false;
        }
        std::erase_if(document.pendingSelect, [&drawn](int id) {
            if (std::find(drawn.begin(), drawn.end(), id) == drawn.end()) {
                return false;
            }
            if (!ImNodes::IsNodeSelected(id)) {
                ImNodes::SelectNode(id);
            }
            return true;
        });

        document.selectedNodes.assign(static_cast<size_t>((std::max)(0, ImNodes::NumSelectedNodes())), 0);
        if (!document.selectedNodes.empty()) {
            ImNodes::GetSelectedNodes(document.selectedNodes.data());
            document.selectedProperty.clear();
        }
        document.selectedLinks.assign(static_cast<size_t>((std::max)(0, ImNodes::NumSelectedLinks())), 0);
        if (!document.selectedLinks.empty()) {
            ImNodes::GetSelectedLinks(document.selectedLinks.data());
        }

        int hoveredNode = 0;
        int hoveredLink = 0;
        const bool nodeHovered = ImNodes::IsNodeHovered(&hoveredNode);
        const bool linkHovered = ImNodes::IsLinkHovered(&hoveredLink);

        HandleLinkEvents(document);

        if (editorHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            if (nodeHovered) {
                menuNode_ = hoveredNode;
                if (!ImNodes::IsNodeSelected(hoveredNode)) {
                    ImNodes::ClearNodeSelection();
                    ImNodes::SelectNode(hoveredNode);
                    document.selectedNodes = { hoveredNode };
                }
                ImGui::OpenPopup(kNodeMenuPopup);
            } else if (!linkHovered) {
                OpenSearch(document, std::nullopt);
            }
        }
        HandleCanvasShortcuts(document);

        // Blackboard のプロパティを落とすと Property ノードを置く
        const ImRect canvasRect(canvasMin, ImVec2(canvasMin.x + size.x, canvasMin.y + size.y));
        if (ImGui::BeginDragDropTargetCustom(canvasRect, ImGui::GetID("##ShaderGraphDrop"))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPropertyPayload)) {
                const auto* data = static_cast<const char*>(payload->Data);
                const std::string id(data, strnlen(data, static_cast<size_t>(payload->DataSize)));
                if (document.graph.FindProperty(id)) {
                    Node node;
                    node.id = document.graph.NextNodeId();
                    node.type = "Property";
                    node.property = id;
                    node.x = document.mouseGridX;
                    node.y = document.mouseGridY;
                    document.graph.nodes.push_back(node);
                    document.placeNodes.insert(node.id);
                    document.clearSelection = true;
                    document.pendingSelect = { node.id };
                    document.pendingLabel = "プロパティを置く";
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopup(kNodeMenuPopup)) {
            if (ImGui::MenuItem("複製", "Ctrl+D")) {
                CopySelection(document);
                if (const Node* const node = document.graph.FindNode(menuNode_)) {
                    Paste(document, node->x + 40.0f, node->y + 40.0f);
                }
            }
            if (ImGui::MenuItem("消す", "Delete")) {
                DeleteSelection(document);
            }
            if (ImGui::MenuItem("このノードを映す")) {
                document.previewSelected = true;
            }
            ImGui::EndPopup();
        }
        DrawSearch(document);
    }

    void ShaderGraphEditor::DrawNode(Document& document, Node& node)
    {
        const NodeDesc* const desc = FindNodeDesc(node.type);
        const auto resolvedIt = document.resolution.nodes.find(node.id);
        const ResolvedNode* const resolved = resolvedIt != document.resolution.nodes.end() ? &resolvedIt->second : nullptr;
        const bool hasError = document.errorNodes.contains(node.id);
        const bool isMaster = node.type == kMasterNodeType;
        const bool isProperty = node.type == "Property";

        int pushedColors = 0;
        const auto pushTitle = [&pushedColors](const ImVec4& color) {
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, ToU32(color));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ToU32(Lighten(color, 0.12f)));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ToU32(Lighten(color, 0.25f)));
            pushedColors = 3;
        };
        if (hasError || !desc) {
            pushTitle(Theme::kError);
        } else if (isMaster) {
            pushTitle(Theme::kAccentMuted);
        } else if (isProperty) {
            pushTitle(Theme::FromSrgb(74, 58, 104));
        }

        ImGui::PushID(node.id);
        ImNodes::BeginNode(node.id);
        ImNodes::BeginNodeTitleBar();
        if (!desc) {
            ImGui::Text("? %s", node.type.c_str());
        } else if (isProperty) {
            const Property* const property = document.graph.FindProperty(node.property);
            const std::string title = property ? (property->display.empty() ? property->name : property->display) : "（無いプロパティ）";
            ImGui::TextUnformatted(title.c_str());
        } else {
            ImGui::TextUnformatted(desc->title.data(), desc->title.data() + desc->title.size());
        }
        ImNodes::EndNodeTitleBar();

        if (!desc) {
            ImGui::TextDisabled("知らない種類のノードです");
            ImNodes::EndNode();
            ImGui::PopID();
            for (int index = 0; index < pushedColors; ++index) {
                ImNodes::PopColorStyle();
            }
            return;
        }

        // 選択肢と文字の設定
        for (const SettingDesc& setting : desc->settings) {
            const std::string current = SettingValue(node, setting);
            ImGui::PushID(setting.name.data(), setting.name.data() + setting.name.size());
            if (!setting.choices.empty()) {
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::BeginCombo("##setting", current.c_str())) {
                    for (const std::string_view choice : setting.choices) {
                        const std::string text(choice);
                        if (ImGui::Selectable(text.c_str(), text == current)) {
                            node.settings[std::string(setting.name)] = text;
                            document.pendingLabel = "設定を変える";
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                std::string text = current;
                ImGui::SetNextItemWidth(72.0f);
                if (UI::InputString("##setting", text)) {
                    node.settings[std::string(setting.name)] = text;
                    document.pendingLabel = "設定を変える";
                }
            }
            ImGui::PopID();
        }

        if (node.type == "Color") {
            std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
            if (const auto found = node.values.find("Color"); found != node.values.end()) {
                color = found->second;
            }
            if (ImGui::ColorEdit4("##color", color.data(),
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                node.values["Color"] = color;
                document.pendingLabel = "色を変える";
            }
        }

        // 入力（左）
        if (!desc->inputs.empty()) {
            ImGui::BeginGroup();
            for (size_t index = 0; index < desc->inputs.size(); ++index) {
                const PinDesc& pin = desc->inputs[index];
                const ValueType type = resolved && index < resolved->inputs.size()
                    ? resolved->inputs[index] : (pin.type == ValueType::Dynamic ? ValueType::Float : pin.type);
                const bool connected = document.graph.FindLinkTo(node.id, pin.name) != nullptr;
                ImNodes::PushColorStyle(ImNodesCol_Pin, ToU32(PinColor(type)));
                ImNodes::BeginInputAttribute(InputPinId(node.id, index), PinShape(type, connected));
                ImGui::TextUnformatted(pin.name.data(), pin.name.data() + pin.name.size());
                if (!connected) {
                    if (const char* const hint = BindingHint(pin.binding)) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", hint);
                    } else if (type == ValueType::Float) {
                        std::array<float, 4> value = InputValue(node, pin);
                        ImGui::SameLine();
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::SetNextItemWidth(kInlineFieldWidth);
                        if (ImGui::DragFloat("##value", &value[0], 0.01f, 0.0f, 0.0f, "%.3g")) {
                            node.values[std::string(pin.name)] = value;
                            document.pendingLabel = "値を変える";
                        }
                        ImGui::PopID();
                    }
                }
                ImNodes::EndInputAttribute();
                ImNodes::PopColorStyle();
            }
            ImGui::EndGroup();
            if (!desc->outputs.empty()) {
                ImGui::SameLine(0.0f, 20.0f);
            }
        }

        // 出力（右寄せ）
        if (!desc->outputs.empty()) {
            float widest = 0.0f;
            for (const PinDesc& pin : desc->outputs) {
                widest = (std::max)(widest, ImGui::CalcTextSize(pin.name.data(), pin.name.data() + pin.name.size()).x);
            }
            ImGui::BeginGroup();
            for (size_t index = 0; index < desc->outputs.size(); ++index) {
                const PinDesc& pin = desc->outputs[index];
                const ValueType type = resolved && index < resolved->outputs.size()
                    ? resolved->outputs[index] : (pin.type == ValueType::Dynamic ? ValueType::Float : pin.type);
                ImNodes::PushColorStyle(ImNodesCol_Pin, ToU32(PinColor(type)));
                ImNodes::BeginOutputAttribute(OutputPinId(node.id, index), PinShape(type, true));
                const float width = ImGui::CalcTextSize(pin.name.data(), pin.name.data() + pin.name.size()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (widest - width));
                ImGui::TextUnformatted(pin.name.data(), pin.name.data() + pin.name.size());
                ImNodes::EndOutputAttribute();
                ImNodes::PopColorStyle();
            }
            ImGui::EndGroup();
        }

        ImNodes::EndNode();
        ImGui::PopID();
        for (int index = 0; index < pushedColors; ++index) {
            ImNodes::PopColorStyle();
        }
    }

    void ShaderGraphEditor::HandleLinkEvents(Document& document)
    {
        int start = 0;
        int end = 0;
        if (ImNodes::IsLinkCreated(&start, &end)) {
            Connect(document, start, end);
        }
        int link = 0;
        if (ImNodes::IsLinkDestroyed(&link)) {
            const PinRef pin = DecodePin(link);
            const Node* const node = document.graph.FindNode(pin.node);
            const NodeDesc* const desc = node ? FindNodeDesc(node->type) : nullptr;
            if (desc && !pin.output && pin.index < desc->inputs.size()) {
                const std::string_view name = desc->inputs[pin.index].name;
                std::erase_if(document.graph.links, [&pin, name](const Link& candidate) {
                    return candidate.toNode == pin.node && candidate.toPin == name;
                });
                document.pendingLabel = "つながりを外す";
            }
        }
        int dropped = 0;
        if (ImNodes::IsLinkDropped(&dropped, false)) {
            OpenSearch(document, dropped);
        }
    }

    void ShaderGraphEditor::HandleCanvasShortcuts(Document& document)
    {
        if (ImGui::GetIO().WantTextInput) {
            return;
        }
        if (document.canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            OpenSearch(document, std::nullopt);
        }
        if (ImGui::Shortcut(ImGuiKey_Delete) || ImGui::Shortcut(ImGuiKey_Backspace)) {
            DeleteSelection(document);
        }
        if (ImGui::Shortcut(ImGuiKey_F)) {
            document.frameRequested = true;
        }
    }

    ValueType ShaderGraphEditor::OutputType(const Document& document, const Node& node, size_t index) const
    {
        if (const auto resolved = document.resolution.nodes.find(node.id); resolved != document.resolution.nodes.end()
            && index < resolved->second.outputs.size()) {
            return resolved->second.outputs[index];
        }
        const NodeDesc* const desc = FindNodeDesc(node.type);
        if (!desc || index >= desc->outputs.size()) {
            return ValueType::Float;
        }
        return desc->outputs[index].type;
    }

    void ShaderGraphEditor::Connect(Document& document, int outputPin, int inputPin)
    {
        PinRef from = DecodePin(outputPin);
        PinRef to = DecodePin(inputPin);
        if (!from.output && to.output) {
            std::swap(from, to);
        }
        if (!from.output || to.output) {
            return;
        }
        const Node* const fromNode = document.graph.FindNode(from.node);
        const Node* const toNode = document.graph.FindNode(to.node);
        const NodeDesc* const fromDesc = fromNode ? FindNodeDesc(fromNode->type) : nullptr;
        const NodeDesc* const toDesc = toNode ? FindNodeDesc(toNode->type) : nullptr;
        if (!fromDesc || !toDesc || from.index >= fromDesc->outputs.size() || to.index >= toDesc->inputs.size()) {
            return;
        }
        if (from.node == to.node || document.graph.Reaches(to.node, from.node)) {
            document.notice = "つなぐと輪になるので、つなぎませんでした。";
            return;
        }
        const ValueType fromType = OutputType(document, *fromNode, from.index);
        const ValueType toType = toDesc->inputs[to.index].type;
        if (!CanConnect(fromType, toType)) {
            document.notice = std::string("種類が合わないので、つなぎませんでした（") + ValueTypeName(fromType)
                + " → " + ValueTypeName(toType) + "）。";
            return;
        }
        const std::string toPin(toDesc->inputs[to.index].name);
        std::erase_if(document.graph.links, [&to, &toPin](const Link& link) {
            return link.toNode == to.node && link.toPin == toPin;
        });
        document.graph.links.push_back(Link{ from.node, std::string(fromDesc->outputs[from.index].name), to.node, toPin });
        document.pendingLabel = "つなぐ";
        document.notice.clear();
    }

    void ShaderGraphEditor::OpenSearch(Document& document, std::optional<int> fromPin)
    {
        search_.open = true;
        search_.filter.clear();
        search_.gridX = document.mouseGridX;
        search_.gridY = document.mouseGridY;
        const ImVec2 mouse = ImGui::GetMousePos();
        search_.screenX = mouse.x;
        search_.screenY = mouse.y;
        search_.fromPin = fromPin;
    }

    bool ShaderGraphEditor::Accepts(const Document& document, const NodeDesc& desc, std::optional<int> fromPin) const
    {
        if (!fromPin) {
            return true;
        }
        const PinRef pin = DecodePin(*fromPin);
        const Node* const source = document.graph.FindNode(pin.node);
        const NodeDesc* const sourceDesc = source ? FindNodeDesc(source->type) : nullptr;
        if (!sourceDesc) {
            return true;
        }
        if (pin.output) {
            const ValueType type = OutputType(document, *source, pin.index);
            return std::any_of(desc.inputs.begin(), desc.inputs.end(),
                [type](const PinDesc& input) { return CanConnect(type, input.type); });
        }
        if (pin.index >= sourceDesc->inputs.size()) {
            return true;
        }
        const ValueType wanted = sourceDesc->inputs[pin.index].type;
        return std::any_of(desc.outputs.begin(), desc.outputs.end(),
            [wanted](const PinDesc& output) { return CanConnect(output.type, wanted); });
    }

    void ShaderGraphEditor::DrawSearch(Document& document)
    {
        if (search_.open) {
            ImGui::OpenPopup(kSearchPopup);
            ImGui::SetNextWindowPos(ImVec2(search_.screenX, search_.screenY));
            search_.open = false;
            search_.focusFilter = true;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(340.0f, 460.0f));
        if (!ImGui::BeginPopup(kSearchPopup)) {
            return;
        }
        if (search_.focusFilter) {
            ImGui::SetKeyboardFocusHere();
            search_.focusFilter = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        UI::InputStringWithHint("##filter", "ノードを探す（英語の名前）", search_.filter);
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

        const std::string filter = Lower(search_.filter);
        const NodeDesc* chosen = nullptr;
        if (ImGui::BeginChild("##results", ImVec2(0.0f, 320.0f))) {
            if (filter.empty()) {
                // 分類の木（`Math/Basic` は Math の下の Basic）
                std::map<std::string, std::map<std::string, std::vector<const NodeDesc*>>> tree;
                std::vector<std::string> order;
                for (const NodeDesc& desc : AllNodes()) {
                    if (!desc.creatable || !Accepts(document, desc, search_.fromPin)) {
                        continue;
                    }
                    const std::string category(desc.category);
                    const size_t slash = category.find('/');
                    const std::string top = category.substr(0, slash);
                    const std::string sub = slash == std::string::npos ? std::string() : category.substr(slash + 1);
                    if (!tree.contains(top)) {
                        order.push_back(top);
                    }
                    tree[top][sub].push_back(&desc);
                }
                for (const std::string& top : order) {
                    if (!ImGui::TreeNode(top.c_str())) {
                        continue;
                    }
                    for (const auto& [sub, descs] : tree[top]) {
                        const bool open = sub.empty() || ImGui::TreeNode(sub.c_str());
                        if (!open) {
                            continue;
                        }
                        for (const NodeDesc* desc : descs) {
                            const std::string title(desc->title);
                            if (ImGui::Selectable(title.c_str())) {
                                chosen = desc;
                            }
                            if (ImGui::IsItemHovered() && !desc->summary.empty()) {
                                ImGui::SetTooltip("%s", std::string(desc->summary).c_str());
                            }
                        }
                        if (!sub.empty()) {
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            } else {
                const NodeDesc* first = nullptr;
                for (const NodeDesc& desc : AllNodes()) {
                    if (!desc.creatable || !Accepts(document, desc, search_.fromPin)) {
                        continue;
                    }
                    if (!Contains(desc.title, filter) && !Contains(desc.type, filter) && !Contains(desc.category, filter)) {
                        continue;
                    }
                    if (!first) {
                        first = &desc;
                    }
                    const std::string title(desc.title);
                    if (ImGui::Selectable(title.c_str(), first == &desc)) {
                        chosen = &desc;
                    }
                    if (ImGui::IsItemHovered() && !desc.summary.empty()) {
                        ImGui::SetTooltip("%s", std::string(desc.summary).c_str());
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", std::string(desc.category).c_str());
                }
                if (enter && first && !chosen) {
                    chosen = first;
                }
            }
        }
        ImGui::EndChild();

        if (chosen) {
            PlaceNode(document, std::string(chosen->type), search_.gridX, search_.gridY, search_.fromPin);
            search_.fromPin.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ShaderGraphEditor::PlaceNode(Document& document, const std::string& type, float x, float y, std::optional<int> fromPin)
    {
        const NodeDesc* const desc = FindNodeDesc(type);
        if (!desc) {
            return;
        }
        Node node;
        node.id = document.graph.NextNodeId();
        node.type = type;
        node.x = x;
        node.y = y;
        const int id = node.id;
        document.graph.nodes.push_back(std::move(node));
        document.placeNodes.insert(id);
        document.clearSelection = true;
        document.pendingSelect = { id };

        if (fromPin) {
            const PinRef pin = DecodePin(*fromPin);
            const Node* const source = document.graph.FindNode(pin.node);
            const NodeDesc* const sourceDesc = source ? FindNodeDesc(source->type) : nullptr;
            if (sourceDesc && pin.output) {
                const ValueType sourceType = OutputType(document, *source, pin.index);
                for (size_t index = 0; index < desc->inputs.size(); ++index) {
                    if (CanConnect(sourceType, desc->inputs[index].type)) {
                        Connect(document, *fromPin, InputPinId(id, index));
                        break;
                    }
                }
            } else if (sourceDesc && pin.index < sourceDesc->inputs.size()) {
                const ValueType wanted = sourceDesc->inputs[pin.index].type;
                for (size_t index = 0; index < desc->outputs.size(); ++index) {
                    if (CanConnect(desc->outputs[index].type, wanted)) {
                        Connect(document, OutputPinId(id, index), *fromPin);
                        break;
                    }
                }
            }
        }
        document.pendingLabel = "ノードを置く";
    }

    void ShaderGraphEditor::DeleteSelection(Document& document)
    {
        bool changed = false;
        for (const int link : document.selectedLinks) {
            const PinRef pin = DecodePin(link);
            const Node* const node = document.graph.FindNode(pin.node);
            const NodeDesc* const desc = node ? FindNodeDesc(node->type) : nullptr;
            if (!desc || pin.output || pin.index >= desc->inputs.size()) {
                continue;
            }
            const std::string_view name = desc->inputs[pin.index].name;
            changed |= std::erase_if(document.graph.links, [&pin, name](const Link& candidate) {
                return candidate.toNode == pin.node && candidate.toPin == name;
            }) > 0;
        }
        for (const int id : document.selectedNodes) {
            const Node* const node = document.graph.FindNode(id);
            if (!node || node->type == kMasterNodeType) {
                continue;
            }
            document.graph.RemoveNode(id);
            changed = true;
        }
        if (changed) {
            document.selectedNodes.clear();
            document.selectedLinks.clear();
            document.clearSelection = true;
            document.pendingLabel = "消す";
        }
    }

    void ShaderGraphEditor::CopySelection(const Document& document)
    {
        Graph clip;
        clip.target = document.graph.target;
        for (const int id : document.selectedNodes) {
            const Node* const node = document.graph.FindNode(id);
            if (!node || node->type == kMasterNodeType) {
                continue;
            }
            clip.nodes.push_back(*node);
            if (const Property* const property = document.graph.FindProperty(node->property);
                property && !clip.FindProperty(property->id)) {
                clip.properties.push_back(*property);
            }
        }
        if (clip.nodes.empty()) {
            return;
        }
        for (const Link& link : document.graph.links) {
            if (clip.FindNode(link.fromNode) && clip.FindNode(link.toNode)) {
                clip.links.push_back(link);
            }
        }
        clipboard_ = WriteGraph(clip).dump();
    }

    void ShaderGraphEditor::Paste(Document& document, float x, float y)
    {
        if (clipboard_.empty()) {
            return;
        }
        const json root = json::parse(clipboard_, nullptr, false);
        Graph clip;
        std::vector<std::string> warnings;
        if (root.is_discarded() || !ReadGraph(root, clip, warnings)) {
            return;
        }
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        for (const Node& node : clip.nodes) {
            if (node.type != kMasterNodeType) {
                minX = (std::min)(minX, node.x);
                minY = (std::min)(minY, node.y);
            }
        }
        if (minX == FLT_MAX) {
            return;
        }

        std::map<int, int> ids;
        std::vector<int> created;
        int next = document.graph.NextNodeId();
        for (const Node& source : clip.nodes) {
            if (source.type == kMasterNodeType) {
                continue;
            }
            Node node = source;
            if (node.type == "Property") {
                const Property* const original = clip.FindProperty(source.property);
                const Property* const same = document.graph.FindProperty(source.property);
                if (!original) {
                    continue;
                }
                if (same && same->name == original->name) {
                    node.property = same->id;
                } else if (const Property* const byName = document.graph.FindPropertyByName(original->name)) {
                    node.property = byName->id;
                } else {
                    Property added = *original;
                    added.id = document.graph.NextPropertyId();
                    node.property = added.id;
                    document.graph.properties.push_back(std::move(added));
                }
            }
            node.id = next++;
            node.x = source.x - minX + x;
            node.y = source.y - minY + y;
            ids[source.id] = node.id;
            created.push_back(node.id);
            document.placeNodes.insert(node.id);
            document.graph.nodes.push_back(std::move(node));
        }
        for (const Link& link : clip.links) {
            const auto from = ids.find(link.fromNode);
            const auto to = ids.find(link.toNode);
            if (from != ids.end() && to != ids.end()) {
                document.graph.links.push_back(Link{ from->second, link.fromPin, to->second, link.toPin });
            }
        }
        if (!created.empty()) {
            document.clearSelection = true;
            document.pendingSelect = created;
            document.pendingLabel = "貼り付ける";
        }
    }

    // ---------------------------------------------------------------- Blackboard

    void ShaderGraphEditor::AddProperty(Document& document, PropertyType type)
    {
        Property property;
        property.id = document.graph.NextPropertyId();
        property.type = type;
        const std::string base = PropertyBaseName(type);
        property.name = base;
        for (int suffix = 1; document.graph.FindPropertyByName(property.name); ++suffix) {
            property.name = base + std::to_string(suffix);
        }
        property.display = property.name;
        if (type == PropertyType::Color) {
            property.value = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        document.selectedProperty = property.id;
        document.graph.properties.push_back(std::move(property));
        document.clearSelection = true;
        document.pendingLabel = "プロパティを足す";
    }

    void ShaderGraphEditor::RemoveProperty(Document& document, const std::string& id)
    {
        std::vector<int> nodes;
        for (const Node& node : document.graph.nodes) {
            if (node.type == "Property" && node.property == id) {
                nodes.push_back(node.id);
            }
        }
        for (const int node : nodes) {
            document.graph.RemoveNode(node);
        }
        std::erase_if(document.graph.properties, [&id](const Property& property) { return property.id == id; });
        if (document.selectedProperty == id) {
            document.selectedProperty.clear();
        }
        document.clearSelection = true;
        document.pendingLabel = "プロパティを消す";
    }

    void ShaderGraphEditor::DrawBlackboard(Document& document)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Blackboard");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() + ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
            ImGui::OpenPopup(kAddPropertyPopup);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("プロパティを足す");
        }
        if (ImGui::BeginPopup(kAddPropertyPopup)) {
            for (const PropertyType type : kPropertyTypes) {
                if (ImGui::MenuItem(PropertyTypeLabel(type))) {
                    AddProperty(document, type);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        std::string removeId;
        for (const Property& property : document.graph.properties) {
            ImGui::PushID(property.id.c_str());
            const std::string label = property.display.empty() ? property.name : property.display;
            if (ImGui::Selectable(label.c_str(), document.selectedProperty == property.id)) {
                document.selectedProperty = property.id;
                document.clearSelection = true;
                document.selectedNodes.clear();
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(kPropertyPayload, property.id.c_str(), property.id.size() + 1);
                ImGui::TextUnformatted(label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("消す")) {
                    removeId = property.id;
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", PropertyTypeLabel(property.type));
            ImGui::PopID();
        }
        if (!removeId.empty()) {
            RemoveProperty(document, removeId);
        }
        if (document.graph.properties.empty()) {
            UI::Hint("＋ で .mat とスクリプトから変えられる値を足します。");
        } else {
            UI::Hint("名前をキャンバスへドラッグすると置けます。");
        }
    }

    // ---------------------------------------------------------------- Inspector

    void ShaderGraphEditor::DrawInspector(Document& document)
    {
        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();
        if (document.selectedNodes.size() == 1) {
            if (Node* const node = document.graph.FindNode(document.selectedNodes.front())) {
                DrawNodeInspector(document, *node);
                return;
            }
        }
        if (document.selectedNodes.size() > 1) {
            UI::HintF("%d 個のノードを選んでいます。", static_cast<int>(document.selectedNodes.size()));
            return;
        }
        if (Property* const property = document.graph.FindProperty(document.selectedProperty)) {
            DrawPropertyInspector(document, *property);
            return;
        }
        UI::Hint("ノードかプロパティを選ぶと、ここで値を変えられます。");
    }

    void ShaderGraphEditor::DrawNodeInspector(Document& document, Node& node)
    {
        const NodeDesc* const desc = FindNodeDesc(node.type);
        if (!desc) {
            ImGui::TextColored(Theme::kError, "知らない種類のノード: %s", node.type.c_str());
            return;
        }
        ImGui::TextUnformatted(desc->title.data(), desc->title.data() + desc->title.size());
        if (!desc->summary.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextWrapped("%s", std::string(desc->summary).c_str());
            ImGui::PopStyleColor();
        }
        for (const NodeIssue& issue : document.issues) {
            if (issue.node == node.id) {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kError);
                ImGui::TextWrapped("%s", issue.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::Spacing();

        if (node.type == "Property") {
            if (Property* const property = document.graph.FindProperty(node.property)) {
                DrawPropertyInspector(document, *property);
            }
            return;
        }

        ImGui::PushID(node.id);
        if (node.type == "Color") {
            std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
            if (const auto found = node.values.find("Color"); found != node.values.end()) {
                color = found->second;
            }
            Row("色");
            if (ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                node.values["Color"] = color;
                document.pendingLabel = "色を変える";
            }
        }

        for (const SettingDesc& setting : desc->settings) {
            const std::string current = SettingValue(node, setting);
            const std::string label(setting.label.empty() ? setting.name : setting.label);
            ImGui::PushID(setting.name.data(), setting.name.data() + setting.name.size());
            Row(label.c_str());
            if (!setting.choices.empty()) {
                if (ImGui::BeginCombo("##setting", current.c_str())) {
                    for (const std::string_view choice : setting.choices) {
                        const std::string text(choice);
                        if (ImGui::Selectable(text.c_str(), text == current)) {
                            node.settings[std::string(setting.name)] = text;
                            document.pendingLabel = "設定を変える";
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                std::string text = current;
                if (UI::InputString("##setting", text)) {
                    node.settings[std::string(setting.name)] = text;
                    document.pendingLabel = "設定を変える";
                }
            }
            ImGui::PopID();
        }

        const auto resolvedIt = document.resolution.nodes.find(node.id);
        const ResolvedNode* const resolved = resolvedIt != document.resolution.nodes.end() ? &resolvedIt->second : nullptr;
        bool header = false;
        for (size_t index = 0; index < desc->inputs.size(); ++index) {
            const PinDesc& pin = desc->inputs[index];
            if (document.graph.FindLinkTo(node.id, pin.name)) {
                continue;
            }
            if (!header) {
                ImGui::SeparatorText("つないでいない入力");
                header = true;
            }
            const std::string name(pin.name);
            if (const char* const hint = BindingHint(pin.binding)) {
                Row(name.c_str());
                ImGui::TextDisabled("%s", hint);
                continue;
            }
            const ValueType type = resolved && index < resolved->inputs.size() ? resolved->inputs[index] : pin.type;
            const int width = (std::max)(1, WidthOf(type));
            std::array<float, 4> value = InputValue(node, pin);
            ImGui::PushID(static_cast<int>(index));
            Row(name.c_str());
            if (ImGui::DragScalarN("##value", ImGuiDataType_Float, value.data(), width, 0.01f, nullptr, nullptr, "%.3f")) {
                node.values[name] = value;
                document.pendingLabel = "値を変える";
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    bool ShaderGraphEditor::DrawPropertyInspector(Document& document, Property& property)
    {
        bool changed = false;
        ImGui::PushID(property.id.c_str());

        Row("名前");
        std::string name = property.name;
        if (UI::InputString("##name", name, ImGuiInputTextFlags_EnterReturnsTrue) && name != property.name) {
            std::string error;
            if (!IsValidPropertyName(name, &error)) {
                document.notice = "名前を変えませんでした: " + error;
            } else if (document.graph.FindPropertyByName(name)) {
                document.notice = "名前を変えませんでした: 同じ名前のプロパティがあります";
            } else {
                property.name = name;
                document.pendingLabel = "プロパティの名前を変える";
                document.notice.clear();
                changed = true;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("シェーダの変数と .mat のキーに使う名前（Enter で決めます）");
        }

        Row("表示名");
        if (UI::InputString("##display", property.display)) {
            document.pendingLabel = "表示名を変える";
            changed = true;
        }

        Row("種類");
        if (ImGui::BeginCombo("##type", PropertyTypeLabel(property.type))) {
            for (const PropertyType type : kPropertyTypes) {
                if (ImGui::Selectable(PropertyTypeLabel(type), type == property.type) && type != property.type) {
                    property.type = type;
                    property.value = type == PropertyType::Color
                        ? std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } : std::array<float, 4>{};
                    document.pendingLabel = "プロパティの種類を変える";
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        Row("既定値");
        switch (property.type) {
        case PropertyType::Float:
            if (property.hasRange) {
                changed |= ImGui::SliderFloat("##value", &property.value[0], property.rangeMin, property.rangeMax, "%.3f",
                    ImGuiSliderFlags_AlwaysClamp);
            } else {
                changed |= ImGui::DragFloat("##value", &property.value[0], 0.01f);
            }
            break;
        case PropertyType::Vector2:
            changed |= ImGui::DragFloat2("##value", property.value.data(), 0.01f);
            break;
        case PropertyType::Vector3:
            changed |= ImGui::DragFloat3("##value", property.value.data(), 0.01f);
            break;
        case PropertyType::Vector4:
            changed |= ImGui::DragFloat4("##value", property.value.data(), 0.01f);
            break;
        case PropertyType::Color:
            changed |= ImGui::ColorEdit4("##value", property.value.data(),
                ImGuiColorEditFlags_Float | (property.hdr ? ImGuiColorEditFlags_HDR : ImGuiColorEditFlags_None));
            break;
        case PropertyType::Texture2D:
            changed |= TexturePathField("##texture", property.texture);
            ImGui::Dummy(ImVec2(kLabelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::SameLine();
            changed |= ImGui::Checkbox("データとして読む（リニア）", &property.linear);
            break;
        }

        if (property.type == PropertyType::Float) {
            Row("範囲");
            changed |= ImGui::Checkbox("##range", &property.hasRange);
            if (property.hasRange) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                float range[2] = { property.rangeMin, property.rangeMax };
                if (ImGui::DragFloat2("##rangeValues", range, 0.01f)) {
                    property.rangeMin = range[0];
                    property.rangeMax = (std::max)(range[0], range[1]);
                    changed = true;
                }
            }
        }
        if (property.type == PropertyType::Color) {
            Row("HDR");
            changed |= ImGui::Checkbox("##hdr", &property.hdr);
        }

        ImGui::PopID();
        if (changed && document.pendingLabel.empty()) {
            document.pendingLabel = "プロパティを変える";
        }
        return changed;
    }

    // ---------------------------------------------------------------- プレビューと知らせ

    void ShaderGraphEditor::DrawPreview(Document& document)
    {
        ImGui::TextUnformatted("Main Preview");
        const float side = (std::min)(ImGui::GetContentRegionAvail().x, static_cast<float>(ShaderGraphPreview::kSize));
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImDrawList* const drawList = ImGui::GetWindowDrawList();

        // 透けたところが分かるように市松模様を敷く
        constexpr int kCells = 16;
        const float cell = side / static_cast<float>(kCells);
        for (int y = 0; y < kCells; ++y) {
            for (int x = 0; x < kCells; ++x) {
                const ImVec2 min(position.x + cell * static_cast<float>(x), position.y + cell * static_cast<float>(y));
                drawList->AddRectFilled(min, ImVec2(min.x + cell, min.y + cell),
                    ToU32(((x + y) % 2 == 0) ? Theme::kControl : Theme::kHover));
            }
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE texture = preview_.GetTexture();
        if (texture.ptr != 0 && document.previewRevision > 0) {
            drawList->AddImage(static_cast<ImTextureID>(texture.ptr), position, ImVec2(position.x + side, position.y + side));
        }
        ImGui::Dummy(ImVec2(side, side));
        if (!preview_.GetError().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kError);
            ImGui::TextWrapped("%s", preview_.GetError().c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Checkbox("選んだノードを映す", &document.previewSelected);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ノードを 1 つ選ぶと、その出力を色として映します。");
        }
        Row("板の画像");
        TexturePathField("##previewTexture", document.previewTexture);
    }

    void ShaderGraphEditor::DrawMessages(Document& document)
    {
        if (!document.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kError);
            ImGui::TextWrapped("%s", document.error.c_str());
            ImGui::PopStyleColor();
        }
        for (const NodeIssue& issue : document.issues) {
            const Node* const node = document.graph.FindNode(issue.node);
            const NodeDesc* const desc = node ? FindNodeDesc(node->type) : nullptr;
            const std::string title = desc ? std::string(desc->title) : std::string("ノード");
            const std::string text = title + " (" + std::to_string(issue.node) + "): " + issue.message;
            ImGui::PushID(&issue);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kWarn);
            if (ImGui::Selectable(text.c_str())) {
                document.clearSelection = true;
                document.pendingSelect = { issue.node };
                document.selectedNodes = { issue.node };
                document.frameRequested = true;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (!document.notice.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextWrapped("%s", document.notice.c_str());
            ImGui::PopStyleColor();
        }
    }

    // ---------------------------------------------------------------- 履歴・検査・保存

    void ShaderGraphEditor::CommitIfChanged(Document& document)
    {
        if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            return;
        }
        document.currentText = GraphText(document.graph);
        if (document.currentText == document.committedText) {
            document.pendingLabel.clear();
            return;
        }
        const std::string label = "シェーダーグラフ: " + (document.pendingLabel.empty() ? std::string("変える") : document.pendingLabel);
        EditorCommandStack::Get().Push(std::make_unique<GraphEditCommand>(label, &document,
            document.committedText, document.currentText));
        document.committedText = document.currentText;
        document.pendingLabel.clear();
    }

    int ShaderGraphEditor::PreviewNode(const Document& document) const
    {
        if (!document.previewSelected || document.selectedNodes.size() != 1) {
            return 0;
        }
        const Node* const node = document.graph.FindNode(document.selectedNodes.front());
        return node && node->type != kMasterNodeType ? node->id : 0;
    }

    void ShaderGraphEditor::UpdateValidation(Document& document)
    {
        const int previewNode = PreviewNode(document);
        const std::string key = document.currentText + '\n' + std::to_string(previewNode);
        if (key != document.requestedKey) {
            document.requestedKey = key;
            GenerateOptions options;
            options.sourcePath = ToAssetText(document.file);
            options.previewNode = previewNode;
            GenerateResult generated = Generate(document.graph, options);
            document.issues = std::move(generated.issues);
            document.errorNodes.clear();
            for (const NodeIssue& issue : document.issues) {
                document.errorNodes.insert(issue.node);
            }
            if (!generated.fatal.empty()) {
                document.error = generated.fatal;
                document.validatePending = false;
                return;
            }
            if (generated.hlsl == document.validatedHlsl) {
                document.error.clear();
                document.validatePending = false;
                return;
            }
            document.pendingHlsl = std::move(generated.hlsl);
            document.pendingLines = std::move(generated.nodeLines);
            document.changeTime = ImGui::GetTime();
            document.validatePending = true;
        }
        if (document.validatePending && ImGui::GetTime() - document.changeTime >= kValidateDelay) {
            document.validatePending = false;
            Validate(document);
        }
    }

    void ShaderGraphEditor::Validate(Document& document)
    {
        if (!compiler_) {
            return;
        }
        const std::string key = ScratchKey(document);
        const CompileCheck check = CheckCompile(*compiler_, document.pendingHlsl, ScratchShaderPath(key + ".check"));
        if (!check.ok) {
            document.error = check.message;
            for (const int line : FindErrorLines(check.message)) {
                for (const auto& [node, range] : document.pendingLines) {
                    if (line >= range.first && line <= range.second) {
                        document.errorNodes.insert(node);
                    }
                }
            }
            return;
        }
        if (!WriteFileReplacing(ScratchShaderPath(key + ".preview"), document.pendingHlsl)) {
            document.error = "プレビューのシェーダを書けません";
            return;
        }
        document.validatedHlsl = std::move(document.pendingHlsl);
        document.pendingHlsl.clear();
        document.error.clear();
        ++document.previewRevision;
    }

    void ShaderGraphEditor::RebuildPreviewValues(Document& document)
    {
        std::string text;
        for (const Property& property : document.graph.properties) {
            text += property.name + ' ' + ToString(property.type) + ' ' + property.texture + (property.linear ? " l" : " s");
            for (const float value : property.value) {
                text += ' ' + std::to_string(value);
            }
            text += '\n';
        }
        if (text == document.previewValuesText) {
            return;
        }
        document.previewValuesText = std::move(text);
        document.previewValues = ShaderMaterial{};
        for (const Property& property : document.graph.properties) {
            if (property.type == PropertyType::Texture2D) {
                if (!property.texture.empty()) {
                    document.previewValues.SetTexture(property.name, MaterialTexture{ property.texture, property.linear });
                }
                continue;
            }
            document.previewValues.SetNumbers(property.name,
                std::span<const float>(property.value.data(), static_cast<size_t>(WidthOf(property.type))));
        }
    }

    void ShaderGraphEditor::Save(Document& document)
    {
        if (!compiler_) {
            return;
        }
        Logger& log = Logger::GetInstance();
        document.currentText = GraphText(document.graph);
        const SaveResult result = SaveGraph(document.file, document.graph, *compiler_);
        if (result.graphSaved) {
            document.savedText = document.currentText;
        }
        if (!result.error.empty()) {
            document.notice = result.graphSaved
                ? "グラフは保存しました。シェーダは前のままです: " + result.error
                : "保存できません: " + result.error;
            log.Log("シェーダーグラフ「" + document.title + "」: " + document.notice, LogLevel::Warn, LogCategory::Shader);
            return;
        }
        document.notice = "保存しました。";
        if (result.shaderWritten) {
            document.notice += "シェーダを書き直しました。";
        }
        if (result.updatedMaterials > 0) {
            document.notice += ".mat を " + std::to_string(result.updatedMaterials) + " 個直しました。";
        }
        log.Log("シェーダーグラフ「" + document.title + "」: " + document.notice, LogLevel::Info, LogCategory::Shader);
    }

    std::string ShaderGraphEditor::ScratchKey(const Document& document) const
    {
        const std::string guid = AssetDatabase::GetInstance().GetGUID(document.file);
        if (!guid.empty()) {
            return guid;
        }
        return std::to_string(std::hash<std::wstring>{}(document.file.wstring()));
    }

    void ShaderGraphEditor::RunSelfTest()
    {
        Logger& log = Logger::GetInstance();
        if (selfTestRunning_) {
            log.Log("シェーダーグラフの全ノード検査はまだ終わっていません", LogLevel::Warn, LogCategory::Shader);
            return;
        }
        if (selfTest_.joinable()) {
            selfTest_.join();
        }
        selfTestRunning_ = true;
        log.Log("シェーダーグラフの全ノード検査を始めます", LogLevel::Info, LogCategory::Shader);
        selfTest_ = std::jthread([this](std::stop_token stop) {
            Logger& log = Logger::GetInstance();
            ShaderCompiler compiler;
            compiler.Initialize();
            const std::vector<SelfTestCase> cases = BuildSelfTestCases();
            const std::filesystem::path scratch = ScratchShaderPath("SelfTest");
            size_t checked = 0;
            size_t failures = 0;
            for (const SelfTestCase& test : cases) {
                if (stop.stop_requested()) {
                    break;
                }
                ++checked;
                std::string message = test.generated.fatal;
                if (message.empty() && !test.generated.issues.empty()) {
                    message = test.generated.issues.front().message;
                }
                if (message.empty()) {
                    const CompileCheck check = CheckCompile(compiler, test.generated.hlsl, scratch);
                    message = check.ok ? std::string() : check.message;
                }
                if (!message.empty()) {
                    ++failures;
                    log.Log("全ノード検査で失敗: " + test.name + ": " + message, LogLevel::Error, LogCategory::Shader);
                }
            }
            std::error_code ec;
            std::filesystem::remove(scratch, ec);
            const std::string summary = "シェーダーグラフの全ノード検査: " + std::to_string(checked) + " 件中 "
                + std::to_string(failures) + " 件が失敗";
            log.Log(checked < cases.size() ? summary + "（途中で止めました）" : summary,
                failures == 0 ? LogLevel::Info : LogLevel::Error, LogCategory::Shader);
            selfTestRunning_ = false;
        });
    }
}

#endif // CORE_EDITOR
