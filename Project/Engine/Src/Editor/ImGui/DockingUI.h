#pragma once

#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Panel/EditorDockArea.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include <imgui_internal.h>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class SceneDebugEditor;

    /// @brief 既定のドッキングレイアウト
    enum class DockLayoutPreset {
        Standard
    };

    /// @brief 上下のバーに出すエディタの状態
    struct EditorStatus {
        float fps = 0.0f;                   ///< 直近のフレームレート
        std::string sceneName;              ///< 開いているシーン名（空なら出さない）
        bool sceneSaved = true;             ///< 最後の保存から編集していないか
        std::size_t editsSinceSave = 0;     ///< 最後の保存からの編集回数
        bool scriptOk = true;               ///< 直前のスクリプトの読み込みに成功したか
        std::size_t scriptTypeCount = 0;    ///< 読み込めているスクリプトの型数
        std::size_t undoCount = 0;          ///< 取り消せる操作の数
    };

    class DockingUI;

    /// @brief ドックスペースのホストウィンドウが開いている間だけ生きるスコープ
    /// @note 生存中に各パネルを提出する。抜けるとホストウィンドウを閉じる。
    class DockSpaceHostScope {
    public:
        ~DockSpaceHostScope();
        DockSpaceHostScope(const DockSpaceHostScope&) = delete;
        DockSpaceHostScope& operator=(const DockSpaceHostScope&) = delete;

    private:
        DockSpaceHostScope() = default;
        friend class DockingUI;
    };

    /// @brief メニューバー下の 3 段（ツールバー・ドックスペース・ステータスバー）を組む
    class DockingUI {
    public:
        /// @brief ドッキングエリアにウィンドウを登録
        /// @param windowName ウィンドウ名
        /// @param area ドッキングエリア（None は登録しない）
        void RegisterWindow(const std::string& windowName, Editor::DockArea area);

        /// @brief ウィンドウの登録を解除
        /// @param windowName ウィンドウ名
        void UnregisterWindow(const std::string& windowName);

        /// @brief 再生ツールバーを描き、ドックスペースのホストウィンドウを開く
        /// @return ホストウィンドウのスコープ（破棄されるまでの間にパネルを提出する）
        [[nodiscard]] DockSpaceHostScope BeginDockSpaceHost();

        /// @brief ドッキングのセットアップ
        void SetupDockSpace();

        /// @brief 次のフレームで標準レイアウトを組み直す
        /// @details 登録済みウィンドウはすべて既定位置へ戻る。
        void RequestResetLayout() { layoutDirty_ = true; }

        /// @brief 保存された配置を使う（最初のフレームで標準レイアウトを組まない）
        /// @note 保存に載っていないウィンドウだけは、既定の場所へ入れる。
        void UseSavedLayout() { useSavedLayout_ = true; }

        /// @brief レイアウトプリセットを設定
        void SetLayoutPreset(DockLayoutPreset preset);

        /// @brief 現在のレイアウトプリセットを取得
        DockLayoutPreset GetLayoutPreset() const { return layoutPreset_; }

        /// @brief 登録されているウィンドウ一覧を取得（登録順）
        const std::vector<std::pair<std::string, Editor::DockArea>>& GetRegisteredWindows() const { return registeredWindows_; }

        /// @brief ツールバーの高さを取得
        float GetToolbarHeight() const { return toolbarHeight_; }

        /// @brief グリッド表示状態を取得
        bool IsGridVisible() const { return isGridVisible_; }

        /// @brief グリッド表示状態を設定
        void SetGridVisible(bool visible) { isGridVisible_ = visible; }

        /// @brief 上下のバーに出す状態を差し替える（フレームの先頭で呼ぶ）
        void SetStatus(const EditorStatus& status) { status_ = status; }

        /// @brief 今フレームの状態
        const EditorStatus& GetStatus() const { return status_; }

        /// @brief 画面最下部のステータスバーを描画
        void DrawStatusBar();

        /// @brief GPU/CPU タイミングデータを設定（ステータスバーホバー時に表示）
        void SetTimingData(const std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount>& slots) { timingData_ = slots; }

        /// @brief Gameビュー編集用のSceneDebugEditorを設定する
        void SetSceneDebugEditor(SceneDebugEditor* sceneDebugEditor) { sceneDebugEditor_ = sceneDebugEditor; }

    private:
        /// @brief エリアのノードを探す
        /// @details 標準レイアウトを組んだならそのノード。保存された配置なら、
        ///          そのエリアへ登録したウィンドウが今いるノード。
        /// @return 見つからなければ 0
        ImGuiID FindNodeForArea(Editor::DockArea area) const;

        /// @brief 保存に載っていない登録ウィンドウを、エリアのノードへ入れる
        void DockWindowsWithoutSettings();

        /// @brief ドッキングレイアウトを構築
        void BuildDockLayout();

        /// @brief 再生ツールバーを描画（メニューバーの直下）
        void DrawPlaybackToolbar();

        /// @brief ツールバー左側の再生 / 一時停止 / コマ送り
        void DrawPlaybackButtons();

        /// @brief ツールバーのギズモ切り替えと表示トグル
        void DrawGizmoButtons();
        void DrawViewToggles();

        /// @brief ツールバー右端のスクリプト状態と保存状態
        void DrawToolbarStatusChips();

        /// @brief ステータスバーのフレーム内訳ツールチップ
        void DrawTimingTooltip();

    private:
        // 登録されたウィンドウとそのエリア。タブの並びが起動ごとに変わらないよう登録順を保つ
        std::vector<std::pair<std::string, Editor::DockArea>> registeredWindows_;
        bool layoutInitialized_ = false; // レイアウトが初期化されたかどうか
        bool layoutDirty_ = false; // レイアウト再構築が必要かどうか
        bool useSavedLayout_ = false; // 保存された配置を使うか
        bool dockNewWindowsPending_ = false; // 保存に載っていないウィンドウを次のフレームで入れるか
        DockLayoutPreset layoutPreset_ = DockLayoutPreset::Standard;

        // エリアごとのノードID
        ImGuiID nodeIds_[Editor::kDockAreaCount] = { 0 };

        // グリッド表示状態。既定はオフで、ツールバーのボタンから出す
        bool isGridVisible_ = false;

        // 上下のバーに出す状態
        EditorStatus status_{};

        // GPU/CPU タイミングデータ（ステータスバーホバー時に表示）
        std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount> timingData_{};

        // Gameビュー編集状態参照
        SceneDebugEditor* sceneDebugEditor_ = nullptr;

        // ツールバーの高さ
        static constexpr float toolbarHeight_ = 32.0f;

        // ステータスバーの高さ
        static constexpr float statusBarHeight_ = 24.0f;
    };
}
