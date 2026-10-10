#include "pch.h"
#include "Editor/Script/EditorGUIState.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Script/EditorScriptBinding.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>
#include <scriptarray/scriptarray.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    /// @brief スクリプトのノードエディタ 1 つの状態（ウィンドウと ID の組ごと）
    struct NodeEditorState
    {
        /// @brief ノード 1 つについて覚えておくこと
        struct NodeRecord
        {
            /// 最後の EndNodeEditor の時点の位置（グリッドの座標）
            Vector2 position{};

            /// スクリプトが持っているはずの位置（BeginNode に渡された値か、GetNodePosition で返した値）
            Vector2 synced{};

            /// 最後に描いたフレーム
            int lastFrame = -1;
        };

        /// @brief このフレームに描くつながり 1 本
        struct LinkRecord
        {
            int id = 0;
            int outputPin = 0;
            int inputPin = 0;
            std::optional<ImVec4> color;
        };

        std::string name;
        ImNodesEditorContext* context = nullptr;

        /// BeginNodeEditor を呼んだ回数
        int frame = 0;
        int flags = 0;

        std::unordered_map<int, NodeRecord> nodes;

        /// このフレームに描いたノード・ピン・つながり
        std::unordered_set<int> frameNodes;
        std::unordered_set<int> framePins;
        std::unordered_set<int> frameLinkIds;
        std::vector<LinkRecord> frameLinks;

        /// ピンが見つからないと一度ログに出したつながり
        std::unordered_set<int> warnedLinks;

        /// 最後の EndNodeEditor で読み取った状態
        std::vector<int> selectedNodes;
        std::vector<int> selectedLinks;
        std::optional<int> hoveredNode;
        std::optional<int> hoveredLink;
        std::optional<int> hoveredPin;
        std::optional<std::pair<int, int>> createdLink;
        std::optional<int> destroyedLink;
        std::optional<int> droppedLinkPin;
        bool hovered = false;
        Vector2 mouse{};

        /// 描いている間の値
        ImVec2 canvasOrigin{};
        ImVec2 canvasSize{};
        bool hosted = false;

        /// 次の EndNodeEditor で行う操作
        std::vector<int> pendingSelect;
        bool pendingClear = false;
        std::optional<int> pendingFocus;
    };

    namespace
    {
        /// ノードの幅の既定値
        constexpr float kDefaultNodeWidth = 160.0f;

        /// ノードの幅の範囲
        constexpr float kMinNodeWidth = 40.0f;
        constexpr float kMaxNodeWidth = 2000.0f;

        /// @brief ノードエディタに付けられる設定（組み合わせは | でつなぐ）
        enum NodeEditorFlags : int
        {
            NodeEditorNone = 0,
            NodeEditorMiniMap = 1 << 0,
            NodeEditorSnapToGrid = 1 << 1,
        };

        /// @brief 開いているノード
        struct OpenNode
        {
            int id = 0;
            ImGuiWindow* window = nullptr;
            float savedWorkMaxX = 0.0f;
            float savedContentMaxX = 0.0f;
            float contentMinX = 0.0f;
            float width = 0.0f;
            bool titleOpen = false;
            bool titleDone = false;
            bool pinOpen = false;
            bool pinSeen = false;
        };

        ImNodesContext* g_context = nullptr;
        std::unordered_map<std::string, std::unique_ptr<NodeEditorState>> g_editors;
        std::optional<OpenNode> g_openNode;
        std::optional<ImVec4> g_nextNodeColor;
        std::optional<ImVec4> g_nextPinColor;

        ImU32 ToU32(const ImVec4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        /// @brief 色を白へ寄せる（ホバーと選択の色）
        ImVec4 Lighten(const ImVec4& color, float amount)
        {
            return ImVec4(color.x + (1.0f - color.x) * amount, color.y + (1.0f - color.y) * amount,
                color.z + (1.0f - color.z) * amount, color.w);
        }

        ImVec4 ToLinear(const Vector4& color)
        {
            return SrgbToLinear(color.x, color.y, color.z, color.w);
        }

        /// @brief imnodes の全体の状態を作り、エディタの見た目に合わせる
        void EnsureContext()
        {
            if (g_context) {
                return;
            }
            ImNodesContext* const previous = ImNodes::GetCurrentContext();
            g_context = ImNodes::CreateContext();
            ImNodes::SetCurrentContext(g_context);

            ImNodesStyle& style = ImNodes::GetStyle();
            style.NodeCornerRounding = 5.0f;
            style.NodePadding = ImVec2(8.0f, 6.0f);
            style.LinkThickness = 2.4f;
            style.PinCircleRadius = 4.0f;
            style.PinQuadSideLength = 8.0f;
            style.PinTriangleSideLength = 10.0f;
            style.GridSpacing = 32.0f;

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

            // Alt + 左ドラッグでも表示を動かせるようにする（中ボタンの無いマウス向け）
            ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
            ImNodes::GetIO().MultipleSelectModifier.Modifier = &ImGui::GetIO().KeyCtrl;

            // つながっているピンからドラッグすると、つながりを外せるようにする
            ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);

            ImNodes::SetCurrentContext(previous);
        }

        /// @brief 呼び出しの間だけ、スクリプト用の imnodes の状態へ切り替える
        class NodeContext
        {
        public:
            explicit NodeContext(const NodeEditorState* editor)
            {
                EnsureContext();
                previous_ = ImNodes::GetCurrentContext();
                ImNodes::SetCurrentContext(g_context);
                if (editor) {
                    ImNodes::EditorContextSet(editor->context);
                }
            }

            ~NodeContext()
            {
                ImNodes::SetCurrentContext(previous_);
            }

            NodeContext(const NodeContext&) = delete;
            NodeContext& operator=(const NodeContext&) = delete;

        private:
            ImNodesContext* previous_ = nullptr;
        };

        /// @brief 要素の型を指定した空の配列を作る（スクリプトの中から呼ばれたときだけ作れる）
        CScriptArray* CreateIntArray(const std::vector<int>& values)
        {
            asIScriptContext* const context = asGetActiveContext();
            asIScriptEngine* const engine = context ? context->GetEngine() : nullptr;
            asITypeInfo* const type = engine ? engine->GetTypeInfoByDecl("array<int>") : nullptr;
            CScriptArray* const array = type ? CScriptArray::Create(type, static_cast<asUINT>(values.size())) : nullptr;
            if (array) {
                for (asUINT i = 0; i < array->GetSize(); ++i) {
                    *static_cast<int*>(array->At(i)) = values[i];
                }
            }
            return array;
        }

        std::string IdText(int id)
        {
            return std::to_string(id);
        }

        /// @brief 一番内側がノードエディタ（ノードの外）でなければスクリプトの例外にする
        NodeEditorState* RequireEditorLevel(const char* function)
        {
            if (!RequireGUI(function)) {
                return nullptr;
            }
            const GUIScopeEntry* const scope = InnermostWindowOrTable();
            if (scope && scope->kind == GUIScopeKind::NodeEditor && FrameState().openNodeEditor) {
                return FrameState().openNodeEditor;
            }
            if (scope && scope->kind == GUIScopeKind::Node) {
                ThrowScriptException(std::string("EditorGUI::") + function + " はノードの外（EndNode の後）で呼びます");
            } else {
                ThrowScriptException(std::string("EditorGUI::") + function + " は BeginNodeEditor と EndNodeEditor の間で呼びます");
            }
            return nullptr;
        }

        /// @brief 一番内側がノード（題名とピンの外）でなければスクリプトの例外にする
        NodeEditorState* RequireNodeLevel(const char* function)
        {
            if (!RequireGUI(function)) {
                return nullptr;
            }
            const GUIScopeEntry* const scope = InnermostWindowOrTable();
            if (!scope || scope->kind != GUIScopeKind::Node || !g_openNode || !FrameState().openNodeEditor) {
                ThrowScriptException(std::string("EditorGUI::") + function + " は BeginNode と EndNode の間で呼びます");
                return nullptr;
            }
            if (g_openNode->titleOpen) {
                ThrowScriptException(std::string("EditorGUI::") + function + " の前に、BeginNodeTitle を EndNodeTitle で閉じてください");
                return nullptr;
            }
            if (g_openNode->pinOpen) {
                ThrowScriptException(std::string("EditorGUI::") + function + " の前に、開いているピンを閉じてください（ピンの中にピンは置けません）");
                return nullptr;
            }
            return FrameState().openNodeEditor;
        }

        /// @brief 状態を読むノードエディタ（開いていればそれ、無ければこの OnGUI で最後に閉じたもの）
        NodeEditorState* QueryTarget(const char* function)
        {
            if (!RequireGUI(function)) {
                return nullptr;
            }
            const GUIFrameState& state = FrameState();
            if (state.openNodeEditor) {
                return state.openNodeEditor;
            }
            if (state.lastNodeEditor) {
                return state.lastNodeEditor;
            }
            ThrowScriptException(std::string("EditorGUI::") + function + " は BeginNodeEditor の後で呼びます");
            return nullptr;
        }

        void PushColor(ImNodesCol item, const ImVec4& color)
        {
            ImNodes::PushColorStyle(item, ToU32(color));
        }

        // ---------------------------------------------------------------- ノードエディタ

        void BeginNodeEditor(const std::string& id, int flags, float height)
        {
            if (!RequireGUI("BeginNodeEditor")) {
                return;
            }
            GUIFrameState& state = FrameState();
            if (state.openNodeEditor) {
                ThrowScriptException("ノードエディタの中に、もう 1 つのノードエディタは置けません");
                return;
            }

            NodeContext scope(nullptr);
            std::unique_ptr<NodeEditorState>& slot = g_editors[state.windowKey + "\n" + id];
            if (!slot) {
                slot = std::make_unique<NodeEditorState>();
                slot->name = id;
                slot->context = ImNodes::EditorContextCreate();
            }
            NodeEditorState& editor = *slot;
            ImNodes::EditorContextSet(editor.context);

            ++editor.frame;
            editor.flags = flags;
            editor.frameNodes.clear();
            editor.framePins.clear();
            editor.frameLinkIds.clear();
            editor.frameLinks.clear();
            editor.createdLink.reset();
            editor.destroyedLink.reset();
            editor.droppedLinkPin.reset();

            ImNodesStyle& style = ImNodes::GetStyle();
            style.Flags = ImNodesStyleFlags_NodeOutline | ImNodesStyleFlags_GridLines | ImNodesStyleFlags_GridLinesPrimary;
            if ((flags & NodeEditorSnapToGrid) != 0) {
                style.Flags |= ImNodesStyleFlags_GridSnapping;
            }

            ImGui::PushID(id.c_str());
            editor.hosted = height > 0.0f;
            if (editor.hosted) {
                ImGui::BeginChild("##NodeEditorHost", ImVec2(0.0f, height), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            }
            ImNodes::BeginNodeEditor();
            editor.canvasOrigin = ImGui::GetCursorScreenPos();
            editor.canvasSize = ImGui::GetWindowSize();

            state.openNodeEditor = &editor;
            g_openNode.reset();
            g_nextNodeColor.reset();
            g_nextPinColor.reset();
            PushGUIScope(GUIScopeKind::NodeEditor, true);
        }

        void CloseNodeEditor()
        {
            GUIFrameState& state = FrameState();
            NodeEditorState* const editor = state.openNodeEditor;
            if (!editor) {
                return;
            }
            NodeContext scope(editor);

            for (const NodeEditorState::LinkRecord& link : editor->frameLinks) {
                if (!editor->framePins.contains(link.outputPin) || !editor->framePins.contains(link.inputPin)) {
                    if (editor->warnedLinks.insert(link.id).second) {
                        Logger::GetInstance().Log("ノードエディタ「" + editor->name + "」: つながり " + IdText(link.id) + " のピン（"
                            + IdText(link.outputPin) + " → " + IdText(link.inputPin) + "）が描かれていないので、つながりを描きませんでした",
                            LogLevel::Warn, LogCategory::Script);
                    }
                    continue;
                }
                if (link.outputPin == link.inputPin) {
                    continue;
                }
                if (link.color) {
                    PushColor(ImNodesCol_Link, *link.color);
                    PushColor(ImNodesCol_LinkHovered, Lighten(*link.color, 0.35f));
                    PushColor(ImNodesCol_LinkSelected, Theme::kWarm);
                }
                ImNodes::Link(link.id, link.outputPin, link.inputPin);
                if (link.color) {
                    ImNodes::PopColorStyle();
                    ImNodes::PopColorStyle();
                    ImNodes::PopColorStyle();
                }
            }

            if ((editor->flags & NodeEditorMiniMap) != 0) {
                ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
            }

            // 寄せるノードを表示の真ん中へ置く
            if (editor->pendingFocus && editor->frameNodes.contains(*editor->pendingFocus)) {
                const int id = *editor->pendingFocus;
                const ImVec2 origin = ImNodes::GetNodeGridSpacePos(id);
                const ImVec2 size = ImNodes::GetNodeDimensions(id);
                ImNodes::EditorContextResetPanning(ImVec2(
                    editor->canvasSize.x * 0.5f - (origin.x + size.x * 0.5f),
                    editor->canvasSize.y * 0.5f - (origin.y + size.y * 0.5f)));
                editor->pendingFocus.reset();
            }

            editor->hovered = ImNodes::IsEditorHovered();
            ImNodes::EndNodeEditor();

            const ImVec2 panning = ImNodes::EditorContextGetPanning();
            const ImVec2 mouse = ImGui::GetMousePos();
            editor->mouse = Vector2{ mouse.x - editor->canvasOrigin.x - panning.x, mouse.y - editor->canvasOrigin.y - panning.y };

            // 描かなかったノードは imnodes が忘れるので、こちらも忘れる
            std::erase_if(editor->nodes, [editor](const auto& entry) { return entry.second.lastFrame != editor->frame; });
            for (auto& [id, record] : editor->nodes) {
                const ImVec2 position = ImNodes::GetNodeGridSpacePos(id);
                record.position = Vector2{ position.x, position.y };
            }

            if (editor->pendingClear) {
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
                editor->pendingClear = false;
            }
            for (const int id : editor->pendingSelect) {
                if (editor->frameNodes.contains(id) && !ImNodes::IsNodeSelected(id)) {
                    ImNodes::SelectNode(id);
                }
            }
            editor->pendingSelect.clear();

            editor->selectedNodes.assign(static_cast<std::size_t>((std::max)(0, ImNodes::NumSelectedNodes())), 0);
            if (!editor->selectedNodes.empty()) {
                ImNodes::GetSelectedNodes(editor->selectedNodes.data());
            }
            editor->selectedLinks.assign(static_cast<std::size_t>((std::max)(0, ImNodes::NumSelectedLinks())), 0);
            if (!editor->selectedLinks.empty()) {
                ImNodes::GetSelectedLinks(editor->selectedLinks.data());
            }

            int id = 0;
            editor->hoveredNode = ImNodes::IsNodeHovered(&id) ? std::optional<int>(id) : std::nullopt;
            editor->hoveredLink = ImNodes::IsLinkHovered(&id) ? std::optional<int>(id) : std::nullopt;
            editor->hoveredPin = ImNodes::IsPinHovered(&id) ? std::optional<int>(id) : std::nullopt;
            int outputPin = 0;
            int inputPin = 0;
            if (ImNodes::IsLinkCreated(&outputPin, &inputPin)) {
                editor->createdLink = std::make_pair(outputPin, inputPin);
            }
            if (ImNodes::IsLinkDestroyed(&id)) {
                editor->destroyedLink = id;
            }
            if (ImNodes::IsLinkDropped(&id, false)) {
                editor->droppedLinkPin = id;
            }

            if (editor->hosted) {
                ImGui::EndChild();
            }
            ImGui::PopID();

            g_openNode.reset();
            g_nextNodeColor.reset();
            g_nextPinColor.reset();
            state.openNodeEditor = nullptr;
            state.lastNodeEditor = editor;
        }

        void EndNodeEditor()
        {
            PopGUIScope(GUIScopeKind::NodeEditor, "EndNodeEditor");
        }

        // ---------------------------------------------------------------- ノード

        void BeginNode(int id, const Vector2& position, float width)
        {
            NodeEditorState* const editor = RequireEditorLevel("BeginNode");
            if (!editor) {
                return;
            }
            if (id == INT_MIN) {
                ThrowScriptException("EditorGUI::BeginNode の番号に int の最小値は使えません");
                return;
            }
            if (!editor->frameNodes.insert(id).second) {
                ThrowScriptException("ノードの番号 " + IdText(id) + " を、同じノードエディタの中で 2 回使っています");
                return;
            }
            width = width > 0.0f ? std::clamp(width, kMinNodeWidth, kMaxNodeWidth) : kDefaultNodeWidth;

            NodeContext scope(editor);

            // 新しく出たノードか、スクリプトが位置を変えたときだけ置き直す（それ以外はドラッグした位置のまま）
            NodeEditorState::NodeRecord& record = editor->nodes[id];
            const bool fresh = record.lastFrame != editor->frame - 1;
            if (fresh || position.x != record.synced.x || position.y != record.synced.y) {
                ImNodes::SetNodeGridSpacePos(id, ImVec2(position.x, position.y));
                record.position = position;
                record.synced = position;
            }
            record.lastFrame = editor->frame;

            const std::optional<ImVec4> color = std::exchange(g_nextNodeColor, std::nullopt);
            if (color) {
                PushColor(ImNodesCol_TitleBar, *color);
                PushColor(ImNodesCol_TitleBarHovered, Lighten(*color, 0.12f));
                PushColor(ImNodesCol_TitleBarSelected, Lighten(*color, 0.25f));
            }
            ImNodes::BeginNode(id);
            if (color) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }

            // ノードの中の部品が、幅いっぱいの部品も含めてノードの幅に収まるようにする
            ImGuiWindow* const window = ImGui::GetCurrentWindow();
            OpenNode node;
            node.id = id;
            node.window = window;
            node.savedWorkMaxX = window->WorkRect.Max.x;
            node.savedContentMaxX = window->ContentRegionRect.Max.x;
            node.contentMinX = ImGui::GetCursorScreenPos().x;
            node.width = width;
            window->WorkRect.Max.x = node.contentMinX + width;
            window->ContentRegionRect.Max.x = node.contentMinX + width;
            g_openNode = node;

            PushGUIScope(GUIScopeKind::Node, true);
        }

        void CloseNode()
        {
            NodeContext scope(FrameState().openNodeEditor);
            if (g_openNode) {
                ImGuiWindow* const window = g_openNode->window;
                window->DC.CursorMaxPos.x = (std::max)(window->DC.CursorMaxPos.x, g_openNode->contentMinX + g_openNode->width);
                window->WorkRect.Max.x = g_openNode->savedWorkMaxX;
                window->ContentRegionRect.Max.x = g_openNode->savedContentMaxX;
            }
            ImNodes::EndNode();
            g_openNode.reset();
        }

        void EndNode()
        {
            PopGUIScope(GUIScopeKind::Node, "EndNode");
        }

        void SetNextNodeColor(const Vector4& color)
        {
            if (RequireGUI("SetNextNodeColor")) {
                g_nextNodeColor = ToLinear(color);
            }
        }

        // ---------------------------------------------------------------- 題名

        bool OpenTitle(const char* function)
        {
            NodeEditorState* const editor = RequireNodeLevel(function);
            if (!editor) {
                return false;
            }
            if (g_openNode->titleDone || g_openNode->pinSeen) {
                ThrowScriptException(std::string("EditorGUI::") + function + " は、ノードごとに 1 回、ピンより先に呼びます");
                return false;
            }
            NodeContext scope(editor);
            ImNodes::BeginNodeTitleBar();
            g_openNode->titleOpen = true;
            PushGUIScope(GUIScopeKind::NodeTitle, true);
            return true;
        }

        void CloseTitle()
        {
            NodeContext scope(FrameState().openNodeEditor);
            ImNodes::EndNodeTitleBar();
            if (g_openNode) {
                g_openNode->titleOpen = false;
                g_openNode->titleDone = true;
            }
        }

        void BeginNodeTitle()
        {
            OpenTitle("BeginNodeTitle");
        }

        void EndNodeTitle()
        {
            PopGUIScope(GUIScopeKind::NodeTitle, "EndNodeTitle");
        }

        void NodeTitle(const std::string& title)
        {
            if (OpenTitle("NodeTitle")) {
                ImGui::TextUnformatted(title.c_str());
                PopGUIScope(GUIScopeKind::NodeTitle, "NodeTitle");
            }
        }

        // ---------------------------------------------------------------- ピン

        bool OpenPin(GUIScopeKind kind, int id, int shape, const char* function)
        {
            NodeEditorState* const editor = RequireNodeLevel(function);
            if (!editor) {
                return false;
            }
            if (shape < ImNodesPinShape_Circle || shape > ImNodesPinShape_QuadFilled) {
                ThrowScriptException(std::string("EditorGUI::") + function + " の形が PinShape のどれでもありません");
                return false;
            }
            if (!editor->framePins.insert(id).second) {
                ThrowScriptException("ピンの番号 " + IdText(id) + " を、同じノードエディタの中で 2 回使っています（ピンの番号はノードをまたいで重ならないようにします）");
                return false;
            }

            NodeContext scope(editor);
            const std::optional<ImVec4> color = std::exchange(g_nextPinColor, std::nullopt);
            if (color) {
                PushColor(ImNodesCol_Pin, *color);
                PushColor(ImNodesCol_PinHovered, Lighten(*color, 0.35f));
            }
            if (kind == GUIScopeKind::InputPin) {
                ImNodes::BeginInputAttribute(id, shape);
            } else {
                ImNodes::BeginOutputAttribute(id, shape);
            }
            if (color) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
            g_openNode->pinOpen = true;
            g_openNode->pinSeen = true;
            PushGUIScope(kind, true);
            return true;
        }

        void ClosePin(GUIScopeKind kind)
        {
            NodeContext scope(FrameState().openNodeEditor);
            if (kind == GUIScopeKind::InputPin) {
                ImNodes::EndInputAttribute();
            } else {
                ImNodes::EndOutputAttribute();
            }
            if (g_openNode) {
                g_openNode->pinOpen = false;
            }
        }

        void BeginInputPin(int id, int shape)
        {
            OpenPin(GUIScopeKind::InputPin, id, shape, "BeginInputPin");
        }

        void EndInputPin()
        {
            PopGUIScope(GUIScopeKind::InputPin, "EndInputPin");
        }

        void BeginOutputPin(int id, int shape)
        {
            OpenPin(GUIScopeKind::OutputPin, id, shape, "BeginOutputPin");
        }

        void EndOutputPin()
        {
            PopGUIScope(GUIScopeKind::OutputPin, "EndOutputPin");
        }

        void InputPin(int id, const std::string& label, int shape)
        {
            if (OpenPin(GUIScopeKind::InputPin, id, shape, "InputPin")) {
                ImGui::TextUnformatted(label.c_str());
                PopGUIScope(GUIScopeKind::InputPin, "InputPin");
            }
        }

        void OutputPin(int id, const std::string& label, int shape)
        {
            if (OpenPin(GUIScopeKind::OutputPin, id, shape, "OutputPin")) {
                // 出口の名前はノードの右端へ寄せる
                const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
                const float available = ImGui::GetContentRegionAvail().x;
                if (available > textWidth) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - textWidth);
                }
                ImGui::TextUnformatted(label.c_str());
                PopGUIScope(GUIScopeKind::OutputPin, "OutputPin");
            }
        }

        void SetNextPinColor(const Vector4& color)
        {
            if (RequireGUI("SetNextPinColor")) {
                g_nextPinColor = ToLinear(color);
            }
        }

        // ---------------------------------------------------------------- つながり

        void AddLink(int id, int outputPin, int inputPin, std::optional<ImVec4> color)
        {
            NodeEditorState* const editor = RequireEditorLevel("Link");
            if (!editor) {
                return;
            }
            if (!editor->frameLinkIds.insert(id).second) {
                ThrowScriptException("つながりの番号 " + IdText(id) + " を、同じノードエディタの中で 2 回使っています");
                return;
            }
            editor->frameLinks.push_back(NodeEditorState::LinkRecord{ id, outputPin, inputPin, color });
        }

        void Link(int id, int outputPin, int inputPin)
        {
            AddLink(id, outputPin, inputPin, std::nullopt);
        }

        void LinkColored(int id, int outputPin, int inputPin, const Vector4& color)
        {
            AddLink(id, outputPin, inputPin, ToLinear(color));
        }

        // ---------------------------------------------------------------- 状態と操作

        Vector2 GetNodePosition(int id)
        {
            NodeEditorState* const editor = QueryTarget("GetNodePosition");
            if (!editor) {
                return Vector2{};
            }
            const auto found = editor->nodes.find(id);
            if (found == editor->nodes.end()) {
                ThrowScriptException("番号 " + IdText(id) + " のノードは描かれていません（EditorGUI::GetNodePosition）");
                return Vector2{};
            }
            found->second.synced = found->second.position;
            return found->second.position;
        }

        CScriptArray* GetSelectedNodes()
        {
            NodeEditorState* const editor = QueryTarget("GetSelectedNodes");
            return CreateIntArray(editor ? editor->selectedNodes : std::vector<int>{});
        }

        CScriptArray* GetSelectedLinks()
        {
            NodeEditorState* const editor = QueryTarget("GetSelectedLinks");
            return CreateIntArray(editor ? editor->selectedLinks : std::vector<int>{});
        }

        /// @brief 閉じた後のノードエディタで、このフレームに描いたノードか
        bool IsDrawnAfterEnd(const NodeEditorState& editor, int id)
        {
            return FrameState().openNodeEditor != &editor && editor.frameNodes.contains(id);
        }

        void SelectNode(int id)
        {
            NodeEditorState* const editor = QueryTarget("SelectNode");
            if (!editor) {
                return;
            }
            if (!IsDrawnAfterEnd(*editor, id)) {
                editor->pendingSelect.push_back(id);
                return;
            }
            NodeContext scope(editor);
            if (!ImNodes::IsNodeSelected(id)) {
                ImNodes::SelectNode(id);
                editor->selectedNodes.push_back(id);
            }
        }

        void ClearNodeSelection()
        {
            NodeEditorState* const editor = QueryTarget("ClearNodeSelection");
            if (!editor) {
                return;
            }
            editor->pendingSelect.clear();
            if (FrameState().openNodeEditor == editor) {
                editor->pendingClear = true;
                return;
            }
            NodeContext scope(editor);
            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
            editor->selectedNodes.clear();
            editor->selectedLinks.clear();
        }

        void FocusNode(int id)
        {
            if (NodeEditorState* const editor = QueryTarget("FocusNode")) {
                editor->pendingFocus = id;
            }
        }

        bool ReadOptional(const std::optional<int>& value, int& out)
        {
            if (!value) {
                return false;
            }
            out = *value;
            return true;
        }

        bool IsNodeHovered(int& id)
        {
            const NodeEditorState* const editor = QueryTarget("IsNodeHovered");
            return editor && ReadOptional(editor->hoveredNode, id);
        }

        bool IsLinkHovered(int& id)
        {
            const NodeEditorState* const editor = QueryTarget("IsLinkHovered");
            return editor && ReadOptional(editor->hoveredLink, id);
        }

        bool IsPinHovered(int& id)
        {
            const NodeEditorState* const editor = QueryTarget("IsPinHovered");
            return editor && ReadOptional(editor->hoveredPin, id);
        }

        bool IsNodeEditorHovered()
        {
            const NodeEditorState* const editor = QueryTarget("IsNodeEditorHovered");
            return editor && editor->hovered;
        }

        Vector2 GetNodeEditorMousePosition()
        {
            const NodeEditorState* const editor = QueryTarget("GetNodeEditorMousePosition");
            return editor ? editor->mouse : Vector2{};
        }

        bool IsLinkCreated(int& outputPin, int& inputPin)
        {
            const NodeEditorState* const editor = QueryTarget("IsLinkCreated");
            if (!editor || !editor->createdLink) {
                return false;
            }
            outputPin = editor->createdLink->first;
            inputPin = editor->createdLink->second;
            return true;
        }

        bool IsLinkDestroyed(int& id)
        {
            const NodeEditorState* const editor = QueryTarget("IsLinkDestroyed");
            return editor && ReadOptional(editor->destroyedLink, id);
        }

        bool IsLinkDropped(int& pin)
        {
            const NodeEditorState* const editor = QueryTarget("IsLinkDropped");
            return editor && ReadOptional(editor->droppedLinkPin, pin);
        }
    }

    void CloseNodeScope(GUIScopeKind kind)
    {
        switch (kind) {
        case GUIScopeKind::NodeEditor: CloseNodeEditor(); break;
        case GUIScopeKind::Node: CloseNode(); break;
        case GUIScopeKind::NodeTitle: CloseTitle(); break;
        case GUIScopeKind::InputPin:
        case GUIScopeKind::OutputPin:
            ClosePin(kind);
            break;
        default: break;
        }
    }

    void ReleaseEditorGUINodeEditors()
    {
        if (!g_context) {
            g_editors.clear();
            return;
        }
        ImNodesContext* const previous = ImNodes::GetCurrentContext();
        ImNodes::SetCurrentContext(g_context);
        for (auto& [key, editor] : g_editors) {
            if (editor->context) {
                ImNodes::EditorContextFree(editor->context);
                editor->context = nullptr;
            }
        }
        g_editors.clear();
        ImNodes::DestroyContext(g_context);
        ImNodes::SetCurrentContext(previous == g_context ? nullptr : previous);
        g_context = nullptr;
        g_openNode.reset();
    }

    void RegisterEditorGUINodes(Script::BindingRegistrar& r)
    {
        r.Enum("NodeEditorFlags");
        r.EnumValue("NodeEditorFlags", "None", NodeEditorNone);
        r.EnumValue("NodeEditorFlags", "MiniMap", NodeEditorMiniMap);
        r.EnumValue("NodeEditorFlags", "SnapToGrid", NodeEditorSnapToGrid);

        r.Enum("PinShape");
        r.EnumValue("PinShape", "Circle", ImNodesPinShape_Circle);
        r.EnumValue("PinShape", "CircleFilled", ImNodesPinShape_CircleFilled);
        r.EnumValue("PinShape", "Triangle", ImNodesPinShape_Triangle);
        r.EnumValue("PinShape", "TriangleFilled", ImNodesPinShape_TriangleFilled);
        r.EnumValue("PinShape", "Quad", ImNodesPinShape_Quad);
        r.EnumValue("PinShape", "QuadFilled", ImNodesPinShape_QuadFilled);

        r.Function("void BeginNodeEditor(const string &in id, int flags = EditorGUI::NodeEditorFlags::None, float height = 0)",
            asFUNCTION(BeginNodeEditor));
        r.Function("void EndNodeEditor()", asFUNCTION(EndNodeEditor));

        r.Function("void BeginNode(int id, const Vector2 &in position, float width = 160)", asFUNCTION(BeginNode));
        r.Function("void EndNode()", asFUNCTION(EndNode));
        r.Function("void SetNextNodeColor(const Vector4 &in color)", asFUNCTION(SetNextNodeColor));
        r.Function("void NodeTitle(const string &in title)", asFUNCTION(NodeTitle));
        r.Function("void BeginNodeTitle()", asFUNCTION(BeginNodeTitle));
        r.Function("void EndNodeTitle()", asFUNCTION(EndNodeTitle));

        r.Function("void InputPin(int id, const string &in label, PinShape shape = EditorGUI::PinShape::CircleFilled)",
            asFUNCTION(InputPin));
        r.Function("void OutputPin(int id, const string &in label, PinShape shape = EditorGUI::PinShape::CircleFilled)",
            asFUNCTION(OutputPin));
        r.Function("void BeginInputPin(int id, PinShape shape = EditorGUI::PinShape::CircleFilled)", asFUNCTION(BeginInputPin));
        r.Function("void EndInputPin()", asFUNCTION(EndInputPin));
        r.Function("void BeginOutputPin(int id, PinShape shape = EditorGUI::PinShape::CircleFilled)", asFUNCTION(BeginOutputPin));
        r.Function("void EndOutputPin()", asFUNCTION(EndOutputPin));
        r.Function("void SetNextPinColor(const Vector4 &in color)", asFUNCTION(SetNextPinColor));

        r.Function("void Link(int id, int outputPin, int inputPin)", asFUNCTION(Link));
        r.Function("void Link(int id, int outputPin, int inputPin, const Vector4 &in color)", asFUNCTION(LinkColored));

        r.Function("Vector2 GetNodePosition(int id)", asFUNCTION(GetNodePosition));
        r.Function("array<int>@ GetSelectedNodes()", asFUNCTION(GetSelectedNodes));
        r.Function("array<int>@ GetSelectedLinks()", asFUNCTION(GetSelectedLinks));
        r.Function("void SelectNode(int id)", asFUNCTION(SelectNode));
        r.Function("void ClearNodeSelection()", asFUNCTION(ClearNodeSelection));
        r.Function("void FocusNode(int id)", asFUNCTION(FocusNode));
        r.Function("bool IsNodeHovered(int &out id)", asFUNCTION(IsNodeHovered));
        r.Function("bool IsLinkHovered(int &out id)", asFUNCTION(IsLinkHovered));
        r.Function("bool IsPinHovered(int &out id)", asFUNCTION(IsPinHovered));
        r.Function("bool IsNodeEditorHovered()", asFUNCTION(IsNodeEditorHovered));
        r.Function("Vector2 GetNodeEditorMousePosition()", asFUNCTION(GetNodeEditorMousePosition));
        r.Function("bool IsLinkCreated(int &out outputPin, int &out inputPin)", asFUNCTION(IsLinkCreated));
        r.Function("bool IsLinkDestroyed(int &out id)", asFUNCTION(IsLinkDestroyed));
        r.Function("bool IsLinkDropped(int &out pin)", asFUNCTION(IsLinkDropped));
    }
}

#endif // CORE_EDITOR
