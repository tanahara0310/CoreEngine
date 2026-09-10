#pragma once

#ifdef USE_IMGUI

#include <cstddef>
#include <string>
#include <vector>

#include "StageCsvDocument.h"
#include "StageProject.h"

#include "Components/Building/MapChipData.h"

namespace CoreEngine {
    class GameDebugUI;
    class SceneManager;
}

namespace GameComponents {
    class MapGeneratorComponent;
}

namespace GameEditors
{
    /// @brief 区画CSVを絵として編集するステージエディタ
    /// @details Inspector の「Stage」タブとして開く。ゲーム側のコンポーネントには手を入れず、
    ///          CSVの読み書きと MapGeneratorComponent の公開APIだけで動く。
    class StageEditorPanel {
    public:
        /// @brief Inspector のタブとして登録する
        /// @param debugUI 登録先のデバッグUI
        /// @param sceneManager 実行中のマップを引くために使う（nullptr なら反映機能だけ無効）
        /// @note 実体は関数内 static。同じ名前で呼び直してもタブは増えない。
        ///       登録直後のタブは非表示なので、メニューの Window > Application > Stage で開く。
        static void Register(CoreEngine::GameDebugUI* debugUI,
            CoreEngine::SceneManager* sceneManager);

        /// @brief 参照を差し替える（シーン管理の生成後に呼ぶ）
        void Initialize(CoreEngine::SceneManager* sceneManager);

        /// @brief タブの中身を描く
        void Draw();

    private:
        // ── 描画 ──────────────────────────────────────────────
        void DrawToolbar();
        void DrawEditTab();
        void DrawFileBrowser();
        void DrawPalette();
        void DrawGridCanvas();
        void DrawRuntimeTab();
        void DrawAreaTab();

        // ── ファイル操作 ──────────────────────────────────────
        void ReloadProject();
        void RebuildBrowseList();
        void OpenCsv(const std::string& path);
        void SaveCsv(const std::string& path);
        void NewDocument(std::size_t sizeX, std::size_t sizeZ);
        void ResizeDocumentToProjectSize();
        void PrepareNewChunkSuggestion();
        int FindAreaIndexForCsvPath(const std::string& path) const;
        bool IsEditingFixedMap() const;
        std::size_t GetInitialMapSizeX() const;
        std::size_t GetInitialMapSizeZ() const;

        // ── 実行中マップ ──────────────────────────────────────
        GameComponents::MapGeneratorComponent* FindMapGenerator() const;
        /// @brief 適用先として指定できるチャンク番号の上限
        /// @details 固定マップの後ろへ置くチャンク番号。遠いチャンクを指定すると、
        ///          そこへ届くまでの地形が一気に生成されてしまう。
        std::size_t GetMaxApplyChunkIndex(GameComponents::MapGeneratorComponent* generator) const;
        void ApplyToRuntime(GameComponents::MapGeneratorComponent* generator, std::size_t startX);
        void CaptureFromRuntime(GameComponents::MapGeneratorComponent* generator, std::size_t startX);

        // ── その他 ────────────────────────────────────────────
        void SetStatus(const std::string& message, bool isError = false);
        std::size_t GetChunkStartX() const;

        CoreEngine::SceneManager* sceneManager_ = nullptr;
        bool initialized_ = false;

        StageProject project_;
        std::string projectPath_ = StageProjectIO::kDefaultPath;
        /// @brief ファイル一覧に出す並び（先頭は固定CSV、以降がエリア）
        std::vector<StageAreaDefinition> browseList_;

        StageCsvDocument document_;

        int selectedBrowseIndex_ = 0;
        int selectedCsvIndex_ = -1;

        GameComponents::MapChipType brush_ = GameComponents::MapChipType::Ground;
        float cellSize_ = 28.0f;
        bool showStartMarker_ = true;
        bool strokeActive_ = false;

        int newSizeX_ = 10;
        int newSizeZ_ = 9;

        int applyChunkIndex_ = 0;
        bool liveApply_ = false;

        int selectedAreaIndex_ = -1;
        char newAreaBuffer_[64] = {};
        char newCsvBuffer_[64] = {};
        char saveAsBuffer_[260] = {};
        std::vector<char> snippetBuffer_;

        std::string statusMessage_;
        bool statusIsError_ = false;
    };
}

#endif // USE_IMGUI
