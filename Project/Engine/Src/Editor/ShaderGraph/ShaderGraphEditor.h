#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
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
            std::map<int, std::pair<int, int>> pendingLines; ///< ノード → 行（負の番号は、そのノードが関数の外に置いた文）
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
            std::string previewTexture;    ///< 板に貼るテクスチャ・ポストエフェクトでは画面の代わりの画像（空なら既定）
            bool previewSelected = false;  ///< 選んだノードの出力を映す

            // グラフの欄（ポストエフェクトの登録）
            std::vector<std::filesystem::path> graphMaterials; ///< このグラフのシェーダを使う .mat
            double graphMaterialsTime = -1.0e9;                 ///< 最後に .mat を探した時刻
            int registerStage = 1;                              ///< Project.json に足すときの段（kStages の番号）
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
        };

        void EnsureNodesContext();
        Document* FindDocument(const void* document);
        Document* ActiveDocument();

        void DrawDocument(Document& document);
        void DrawToolbar(Document& document);
        void DrawCanvas(Document& document);
        void DrawNode(Document& document, Node& node);
        void DrawSearch(Document& document);
        void DrawBlackboard(Document& document);
        void DrawInspector(Document& document);
        void DrawNodeInspector(Document& document, Node& node);
        void DrawFunctionInspector(Document& document, Node& node);
        void DrawFunctionPins(Document& document, Node& node, bool inputs);
        void DrawGraphInspector(Document& document);
        bool DrawPropertyInspector(Document& document, Property& property);
        void DrawPreview(Document& document);
        void DrawMessages(Document& document);
        void DrawCloseDialog();

        void HandleCanvasShortcuts(Document& document);
        void HandleLinkEvents(Document& document);
        ValueType OutputType(const Document& document, const Node& node, size_t index) const;
        void Connect(Document& document, int outputPin, int inputPin);
        void OpenSearch(Document& document, std::optional<int> fromPin);
        bool Accepts(const Document& document, const NodeDesc& desc, std::optional<int> fromPin) const;
        void PlaceNode(Document& document, const std::string& type, float x, float y, std::optional<int> fromPin);
        void DeleteSelection(Document& document);
        void CopySelection(const Document& document);
        void Paste(Document& document, float x, float y);
        void AddProperty(Document& document, PropertyType type);
        void RemoveProperty(Document& document, const std::string& id);

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

        EngineSystem* engine_ = nullptr;
        std::unique_ptr<ShaderCompiler> compiler_;
        ShaderGraphPreview preview_;
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
    };
}

#endif // CORE_EDITOR
