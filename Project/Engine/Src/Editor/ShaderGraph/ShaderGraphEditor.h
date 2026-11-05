#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Editor/ShaderGraph/ShaderGraphAsset.h"
#include "Editor/ShaderGraph/ShaderGraphCodeGen.h"
#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"
#include "Editor/ShaderGraph/ShaderGraphPreview.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <atomic>
#include <d3d12.h>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct ImNodesContext;
struct ImNodesEditorContext;

namespace CoreEngine
{
    class EngineSystem;
    class ShaderCompiler;
}

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief シェーダーグラフの窓（開いたグラフをタブで並べる）
    class ShaderGraphEditor
    {
    public:
        /// @brief 窓の名前（パネルの id）
        static constexpr const char* kPanelId = "Shader Graph";

        /// @brief Project の窓からサブグラフをドラッグするときの名前（中身は `Application/Assets/…` のパス）
        static constexpr const char* kSubGraphPayload = "SHADERSUBGRAPH_FILE";

        static ShaderGraphEditor& Get();

        void Initialize(EngineSystem* engine);
        void Finalize();

        /// @brief 窓の中身を描く
        void Draw();

        /// @brief プレビューの描画を積む（描画の始めに呼ぶ）
        void RecordPreview(ID3D12GraphicsCommandList* commandList);

        /// @brief グラフを開く（開いていればそのタブへ切り替える）
        /// @return 読めたら true
        bool Open(const std::filesystem::path& graphFile);

        /// @brief 保存していない変更のあるグラフがあるか
        bool HasUnsavedChanges() const;

        /// @brief 開いているグラフの中身を、控えた JSON の文へ戻す（Undo / Redo から呼ぶ）
        void RestoreDocument(const void* document, const std::string& text);

    private:
        ShaderGraphEditor() = default;
        ~ShaderGraphEditor() = default;
        ShaderGraphEditor(const ShaderGraphEditor&) = delete;
        ShaderGraphEditor& operator=(const ShaderGraphEditor&) = delete;

        /// @brief 開いているグラフ 1 つ
        struct Document
        {
            std::filesystem::path file;
            std::string title;
            Graph graph;
            ImNodesEditorContext* context = nullptr;

            std::string currentText;   ///< 今の中身（フレームの終わりに作り直す）
            std::string savedText;     ///< 最後に保存（読み込み）したときの中身
            std::string committedText; ///< 最後に Undo の履歴へ積んだときの中身
            std::string pendingLabel;  ///< 次に履歴へ積む操作の名前
            std::string notice;        ///< 窓の下に出す知らせ
            bool selectOnNextFrame = false;

            // ノードの位置と選択（imnodes へ渡すもの）
            bool syncPositions = true;  ///< 全ノードの位置をグラフから入れ直す
            std::set<int> placeNodes;   ///< 位置を入れ直すノード
            bool clearSelection = false;
            std::vector<int> pendingSelect;
            std::vector<int> selectedNodes;
            std::vector<int> selectedLinks;
            std::string selectedProperty; ///< Blackboard で選んだプロパティの id
            Resolution resolution;

            // キャンバス（グリッドの座標）
            float canvasWidth = 0.0f;
            float canvasHeight = 0.0f;
            float mouseGridX = 0.0f;
            float mouseGridY = 0.0f;
            float viewCenterX = 0.0f;
            float viewCenterY = 0.0f;
            bool canvasHovered = false;
            bool frameRequested = false;

            // 検査コンパイル
            std::string requestedKey; ///< 最後に HLSL を作った中身
            std::string pendingHlsl;  ///< 検査を待っている HLSL
            std::multimap<int, std::pair<int, int>> pendingLines; ///< ノード → 行（負の番号は、そのノードが関数の外に置いた文）
            std::string functionFiles;          ///< Custom Function のファイルとその日時
            double functionFilesTime = -1.0e9;  ///< 最後にファイルの日時を見た時刻
            double changeTime = 0.0;
            bool validatePending = false;
            std::string validatedHlsl;
            std::string error;
            std::vector<NodeIssue> issues;
            std::set<int> errorNodes;

            // プレビュー
            uint64_t previewRevision = 0;
            ShaderMaterial previewValues;
            std::string previewValuesText; ///< previewValues を作ったときのプロパティ
            std::string previewTexture;    ///< 板に貼るテクスチャ・ポストエフェクトでは画面の代わりの画像・3D ではモデルの画像（空なら既定）
            bool previewSelected = false;  ///< 選んだノードの出力を映す
            PreviewView previewView;       ///< 3D の形と向き

            // ノードのプレビュー
            std::string nodePreviewSource; ///< nodePreviews を作ったときの中身
            NodePreviewResult nodePreviews; ///< 最後に作ったノードのプレビューの並び（hlsl は nodePreviewHlsl へ移す）
            std::string nodePreviewHlsl;
            uint64_t nodePreviewKey = 0;    ///< nodePreviewHlsl の印（0 なら映すノードが無い）
            double nodePreviewChangeTime = 0.0;
            std::vector<std::pair<uint64_t, std::vector<NodePreviewTile>>> nodePreviewLayouts; ///< 印ごとのタイルの並び（新しいものが後ろ）

            // グループと付箋
            int selectedGroup = 0;  ///< 選んだグループ（0 なら無し）
            int renamingGroup = 0;  ///< 見出しで名前を書き換えているグループ
            bool focusGroupName = false;
            int editingNote = 0;    ///< 文を書き換えている付箋
            bool focusNote = false;
            std::map<int, std::pair<float, float>> nodeSizes; ///< 前のフレームのノードの大きさ（グループの枠に使う）

            // HLSL を見る・書き出す
            bool showHlsl = false;
            std::string hlslSource; ///< hlsl を作ったときの中身
            GenerateResult hlsl;
            std::vector<std::string> hlslLines;
            int hlslScrollNode = 0; ///< 最後に行まで送ったノード
            bool openExport = false;
            std::filesystem::path exportPath;
            std::vector<std::pair<std::filesystem::path, bool>> exportMaterials; ///< 付け替えるか選ぶ .mat

            // 選んだノードをサブグラフにする
            bool openExtract = false;
            std::set<int> extractNodes; ///< 窓を開いたときに選んでいたノード
            std::string extractName;
            std::string extractError;

            // グラフの欄（ポストエフェクトの登録）
            std::vector<std::filesystem::path> graphMaterials; ///< このグラフのシェーダを使う .mat
            double graphMaterialsTime = -1.0e9;                 ///< 最後に .mat を探した時刻
            int registerStage = 1;                              ///< Project.json に足すときの段（kStages の番号）
        };

        /// @brief 検索に出すサブグラフ 1 つ
        struct SubGraphChoice
        {
            std::string name;
            std::string category; ///< 検索の木の場所（`Sub Graphs/…`）
            Node node;            ///< 置くノード（番号は置くときに決める）
        };

        /// @brief ノードの検索を開いたときの状態
        struct SearchState
        {
            bool open = false;
            bool focusFilter = false;
            std::string filter;
            float gridX = 0.0f;
            float gridY = 0.0f;
            float screenX = 0.0f;
            float screenY = 0.0f;
            std::optional<int> fromPin; ///< つながりを落とした元のピン
            std::vector<SubGraphChoice> subGraphs; ///< 開いたときに集めた、置けるサブグラフ
        };

        /// @brief 保存したサブグラフを使うグラフの作り直し
        struct RegenerationRequest
        {
            std::string guid;
            std::string path;
            std::filesystem::path file;
        };

        void EnsureNodesContext();
        Document* FindDocument(const void* document);
        Document* ActiveDocument();

        void DrawDocument(Document& document);
        void DrawToolbar(Document& document);
        void DrawCanvas(Document& document);
        void DrawNode(Document& document, Node& node);
        void DrawStickyNote(Document& document, Node& node);
        void DrawRedirect(Document& document, Node& node);
        void DrawNodePreview(Document& document, Node& node);
        /// @brief グループの枠と見出しを描く（ノードより先に描く）
        /// @return 見出しの上にマウスがあれば true
        bool DrawGroups(Document& document, float originX, float originY);
        void DrawSearch(Document& document);
        void DrawBlackboard(Document& document);
        void DrawInspector(Document& document);
        void DrawNodeInspector(Document& document, Node& node);
        void DrawFunctionInspector(Document& document, Node& node);
        void DrawFunctionPins(Document& document, Node& node, bool inputs);
        void DrawGraphInspector(Document& document);
        void DrawSubGraphInspector(Document& document);
        void DrawOutputsInspector(Document& document, Node& node);
        void DrawGroupInspector(Document& document, Group& group);
        void DrawStickyInspector(Document& document, Node& node);
        bool DrawPropertyInspector(Document& document, Property& property);
        void DrawPreview(Document& document);
        void DrawHlsl(Document& document);
        void DrawExportDialog(Document& document);
        void DrawMessages(Document& document);
        void DrawCloseDialog();

        void HandleCanvasShortcuts(Document& document);
        void HandleLinkEvents(Document& document);
        ValueType OutputType(const Document& document, const Node& node, size_t index) const;
        void Connect(Document& document, int outputPin, int inputPin);
        void OpenSearch(Document& document, std::optional<int> fromPin);
        bool Accepts(const Document& document, const NodeDesc& desc, std::optional<int> fromPin) const;
        void PlaceNode(Document& document, const std::string& type, float x, float y, std::optional<int> fromPin);
        /// @brief ノードに番号を付けて置き、fromPin があればつなぐ
        void AddNode(Document& document, Node node, std::optional<int> fromPin);
        /// @brief 検索に出すサブグラフを集める（輪になるもの・この目標で使えないものは出さない）
        void CollectSubGraphChoices(const Document& document);
        /// @brief Project の窓から落としたサブグラフを置く
        void PlaceSubGraphFile(Document& document, const std::string& assetPath, float x, float y);
        /// @brief サブグラフのノードが指すサブグラフを開く
        void OpenSubGraph(Document& document, const Node& node);
        void DrawExtractDialog(Document& document);
        /// @brief 選んでいたノードをサブグラフへ切り出す
        /// @return 切り出せたら true
        bool ExtractSelection(Document& document);
        void DeleteSelection(Document& document);
        void CopySelection(const Document& document);
        void Paste(Document& document, float x, float y);
        void AddProperty(Document& document, PropertyType type);
        void RemoveProperty(Document& document, const std::string& id);
        void GroupSelection(Document& document);
        void UngroupSelection(Document& document);
        void RemoveGroup(Document& document, int id);
        /// @brief つながり（link は行き先の入力ピンの番号）の途中に Redirect を置く
        void InsertRedirect(Document& document, int link, float x, float y);
        void SetAllPreviews(Document& document, bool open);
        /// @brief ノードのプレビューを作り直し、裏のコンパイルを頼み、描く中身を決める
        void UpdateNodePreviews(Document& document);
        /// @brief 印のタイルの並び（無ければ nullptr）
        const std::vector<NodePreviewTile>* FindPreviewLayout(const Document& document, uint64_t key) const;
        void OpenExportDialog(Document& document);
        void ExportHlsl(Document& document);

        /// @brief 変わっていれば Undo の履歴へ積む（操作の途中なら待つ）
        void CommitIfChanged(Document& document);
        /// @brief プレビューに映すノード（出口なら 0）
        int PreviewNode(const Document& document) const;
        void UpdateValidation(Document& document);
        void Validate(Document& document);
        void RebuildPreviewValues(Document& document);
        void Save(Document& document);
        void CloseDocument(size_t index);

        std::string ScratchKey(const Document& document) const;

        /// @brief 全ノードの検査を裏で始める（結果はログへ出す）
        void RunSelfTest();

        /// @brief サブグラフを使うグラフの作り直しを裏で始める（走っていれば後に回す）
        void StartRegeneration(RegenerationRequest request);
        /// @brief 裏の作り直しが終わっていれば、生成物を書いて知らせる
        void FinishRegeneration();

        EngineSystem* engine_ = nullptr;
        std::unique_ptr<ShaderCompiler> compiler_;
        ShaderGraphPreview preview_;
        ShaderGraphPreview nodePreview_{ "ShaderGraphNodePreview", static_cast<uint32_t>(kNodePreviewAtlas) };
        ShaderGraphCompileWorker compileWorker_;
        uint64_t submittedPreviewKey_ = 0; ///< 最後に裏へ頼んだノードのプレビューの印
        ImNodesContext* nodesContext_ = nullptr;

        std::vector<std::unique_ptr<Document>> documents_;
        size_t activeDocument_ = 0;
        std::optional<size_t> closingDocument_;
        SearchState search_;
        std::string clipboard_;
        int menuNode_ = 0;
        bool focusRequested_ = false;

        ScopedRegistration selfTestMenu_;
        std::jthread selfTest_;
        std::atomic<bool> selfTestRunning_ = false;

        std::jthread regenerate_;
        std::atomic<bool> regenerateDone_ = false;
        std::optional<RegenerationRequest> runningRegeneration_;
        std::vector<RegenerationRequest> queuedRegenerations_;
        std::vector<Regeneration> regenerated_; ///< 裏のスレッドが作ったもの（終わってから読む）
    };
}

#endif // CORE_EDITOR
