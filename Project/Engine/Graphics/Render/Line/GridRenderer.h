#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Graphics/Line/Line.h"
#include "Math/Vector/Vector3.h"
#include <vector>

// 前方宣言
class LineRendererPipeline;
class ICamera;

/// @brief 無限遠グリッドレンダラー（Unity風）
/// @details XZ平面に無限に広がるグリッドを描画

namespace CoreEngine
{
class GridRenderer : public GameObject {
public:
    GridRenderer() = default;
    ~GridRenderer() override = default;

    /// @brief 初期化
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const ICamera* camera) override;

    /// @brief 描画パスタイプを取得
    RenderPassType GetRenderPassType() const override { return RenderPassType::Line; }

    /// @brief ブレンドモードを取得
    BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }

#ifdef _DEBUG
    /// @brief ImGuiデバッグUI
    bool DrawImGui() override;
#endif

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "GridRenderer"; }

    /// @brief グリッドサイズを設定
    /// @param size グリッドのサイズ（カメラからの距離）
    void SetGridSize(float size) { gridSize_ = size; }

    /// @brief グリッド間隔を設定
    /// @param spacing グリッドの間隔
    void SetSpacing(float spacing) { spacing_ = spacing; }

    /// @brief グリッドの表示/非表示を設定
    /// @param visible 表示する場合true
    void SetVisible(bool visible) { visible_ = visible; }

    /// @brief 軸の色を設定（Blender風）
    /// @param xColor X軸の色（赤系）
    /// @param yColor Y軸の色（青系）
    /// @param zColor Z軸の色（緑系）
    void SetAxisColors(const Vector3& xColor, const Vector3& yColor, const Vector3& zColor) {
        xAxisColor_ = xColor;
        yAxisColor_ = yColor;
        zAxisColor_ = zColor;
    }

private:
    /// @brief グリッドラインを生成
    /// @param cameraPosition カメラ位置
    /// @return 生成されたライン配列
    std::vector<Line> GenerateGridLines(const Vector3& cameraPosition);

    float gridSize_ = 100.0f;      // グリッドサイズ（カメラからの距離）
    float spacing_ = 1.0f;         // グリッド間隔
    bool visible_ = true;          // 表示フラグ
    int majorLineInterval_ = 10;   // 太いラインの間隔

    // フェード設定
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
};
}
