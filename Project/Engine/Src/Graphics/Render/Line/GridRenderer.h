#pragma once

#include "Graphics/Render/Line/ILineSource.h"
#include "Graphics/Line/Line.h"
#include "Math/Vector/Vector3.h"
#include <vector>

namespace CoreEngine
{
class EngineSystem;

/// @brief XZ 平面に無限に広がるエディタ用グリッド（Unity 風）。
/// @details GameObject ではなく `ILineSource`。GridFeature が所有し、Line パスの
///          フラッシュ時に `SubmitLines` でライン群を供給する。設定 UI は Engine Settings 側。
class GridRenderer : public ILineSource {
public:
    GridRenderer() = default;
    ~GridRenderer() override = default;

    /// @brief Line パスから呼ばれ、カメラ位置に応じたグリッドラインを供給する
    void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) override;

#ifdef USE_IMGUI
    /// @brief Engine Settings に「Grid」パネルを登録する（プロセスで一度だけ）
    /// @details パネルはファイルスコープの「現在アクティブなグリッド」を読むだけで
    ///          何もキャプチャしない（GameDebugUI に登録解除 API が無いため）。
    static void EnsureSettingsPanelRegistered(EngineSystem* engine);

    /// @brief このグリッドをパネルの編集対象にする（nullptr で解除）
    static void SetActiveForSettingsPanel(GridRenderer* grid);
#endif

    /// @brief グリッドサイズを設定（カメラからの距離）
    void SetGridSize(float size) { gridSize_ = size; }

    /// @brief グリッド間隔を設定
    void SetSpacing(float spacing) { spacing_ = spacing; }

    /// @brief グリッドの表示/非表示を設定
    void SetVisible(bool visible) { visible_ = visible; }

private:
    /// @brief グリッドラインを生成
    /// @param cameraPosition カメラ位置
    std::vector<Line> GenerateGridLines(const Vector3& cameraPosition);

#ifdef USE_IMGUI
    /// @brief 設定パネルの中身を描画する
    bool DrawSettingsImGui();
#endif

    float gridSize_ = 100.0f;      // グリッドサイズ（カメラからの距離）
    float spacing_ = 1.0f;         // グリッド間隔
    bool visible_ = true;          // 表示フラグ
    int majorLineInterval_ = 10;   // 太いラインの間隔

    // フェード設定
    bool enableDistanceFade_ = false;  // 距離フェード有効フラグ
    float fadeStartDistance_ = 30.0f;  // フェード開始距離
    float fadeEndDistance_ = 80.0f;    // フェード終了距離（完全に透明）

    // Blender風のカラー設定
    Vector3 xAxisColor_ = { 0.85f, 0.0f, 0.0f };   // X軸の色（赤）
    Vector3 yAxisColor_ = { 0.0f, 0.0f, 0.85f };   // Y軸の色（青）
    Vector3 zAxisColor_ = { 0.0f, 0.85f, 0.0f };   // Z軸の色（緑）
    Vector3 normalColor_ = { 0.4f, 0.35f, 0.25f }; // 通常のグリッド色（オレンジっぽいグレー）

    // 透明度設定（統一）
    static constexpr float kAxisAlpha = 1.0f;    // 軸ライン透明度
    static constexpr float kMajorAlpha = 1.0f;   // 太いライン透明度（10本ごと）
    static constexpr float kNormalAlpha = 1.0f;  // 通常のライン透明度

    // 地面や水面とのZ-fightingを避けるため、グリッドをわずかに浮かせる
    static constexpr float kGridPlaneYOffset = 0.01f;
};
}
