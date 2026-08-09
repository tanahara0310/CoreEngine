#pragma once

#include "Graphics/Render/Line/ILineSource.h"
#include "Graphics/Line/Line.h"
#include "Math/Vector/Vector3.h"
#include <vector>

namespace CoreEngine
{
class EngineSystem;

/// @brief XZ 平面へ広がるエディタ用グリッド（Blender / Unity 風のマルチスケール）。
///
/// カメラ高度から「最も細かい格子の間隔」を十進で決め、10 倍ずつ粗い格子を
/// 3 段重ねて描く。段はカメラが上下すると連続的にクロスフェードするため、
/// どの高度でも画面上の線密度がおおよそ一定に保たれる（＝ズームアウトしても
/// 線が潰れてモアレにならず、ズームインしても格子が消えない）。
///
/// 線はカメラ中心の円で切り、縁へ向かって α を落とす。板の「縁」が見えないので
/// 実質的に無限のグリッドとして振る舞う。1 本の Line に持たせられる α は 1 つだけ
/// なので、フェードは線を数セグメントに分割して表現する。
///
/// GameObject ではなく `ILineSource`。GridFeature が所有し、Line パスのフラッシュ時に
/// `SubmitLines` でライン群を供給する。設定 UI は Engine Settings 側。
///
/// @note 「太いライン（major）」の概念は持たない。10 倍ごとの段が同じ位置へ重なって
///       描かれる結果、10 の倍数の線が自動的に濃くなるため（段が繰り上がっても
///       各位置の α の集合が変わらないので、階調が保たれたまま連続に遷移する）。
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

    /// @brief 最も細かい格子の間隔 [m]（これより細かい段は出さない）
    void SetBaseSpacing(float spacing);

    /// @brief 最細の段が使われるカメラ高度 [m]
    /// @details この高度を 10 倍するごとに格子が 1 段粗くなる。
    void SetHeightAtBaseSpacing(float height);

    /// @brief グリッドの表示/非表示を設定
    void SetVisible(bool visible) { visible_ = visible; }

private:
    /// 1 段ぶんの描画パラメータ
    struct GridLevel {
        float spacing = 1.0f; // 格子間隔 [m]
        float radius = 50.0f; // 描画半径 [m]（カメラ中心）
        float alpha = 1.0f;   // この段の基準 α
    };

    /// @brief 1 段ぶんの格子線を積む
    void AppendLevel(std::vector<Line>& out, const Vector3& cameraPosition, const GridLevel& level) const;

    /// @brief 原点を通る軸ライン（X=赤 / Y=青 / Z=緑）を積む
    /// @param radius       X/Z 軸を伸ばす半径（最粗の段に合わせて一番遠くまで見せる）
    /// @param yAxisHalfHeight 垂直な Y 軸の上下の長さ（最細の段に合わせる。最粗に
    ///                        合わせると足元に巨大な青い柱が立ってしまう）
    void AppendAxes(std::vector<Line>& out, const Vector3& cameraPosition,
                    float radius, float yAxisHalfHeight) const;

    /// @brief グリッドライン一式を生成
    std::vector<Line> GenerateGridLines(const Vector3& cameraPosition) const;

#ifdef USE_IMGUI
    /// @brief 設定パネルの中身を描画する
    bool DrawSettingsImGui();
#endif

    float baseSpacing_ = 1.0f;          // 最細の格子間隔 [m]
    float heightAtBaseSpacing_ = 5.0f;  // 最細段を使うカメラ高度 [m]
    float brightness_ = 1.0f;           // 全体の濃さ（α への一括係数）
    bool visible_ = true;               // 表示フラグ

    // Blender 風のカラー設定
    Vector3 xAxisColor_ = { 0.85f, 0.0f, 0.0f };   // X軸の色（赤）
    Vector3 yAxisColor_ = { 0.0f, 0.0f, 0.85f };   // Y軸の色（青）
    Vector3 zAxisColor_ = { 0.0f, 0.85f, 0.0f };   // Z軸の色（緑）
    Vector3 normalColor_ = { 0.4f, 0.35f, 0.25f }; // 通常のグリッド色（オレンジっぽいグレー）

    /// 各段の片側の線数。1 段の描画半径 = spacing × この値。
    /// 総ライン数は「段数 × (2n+1) × 2軸 × 分割数」で高度・カメラ位置に依らず一定になる
    /// （旧実装はワールド原点基準だったため、カメラが離れるほど線数が増え、
    ///  最終的に LineRendererPipeline の頂点上限 65536 を超えて全ラインが消えていた）。
    static constexpr int kHalfLines = 50;

    /// 重ねる段数。最細が消えつつ最粗が現れることで、段の繰り上がりが連続になる
    static constexpr int kLevelCount = 3;

    /// 円周フェードのための 1 本あたりの分割数
    static constexpr int kFadeSegments = 6;

    /// 描画半径のうち、この比率まではフェードを掛けない（0..1）
    static constexpr float kFadeStartRatio = 0.4f;

    /// これ以下の α のセグメントは積まない
    static constexpr float kMinVisibleAlpha = 0.004f;

    /// 軸ライン透明度
    static constexpr float kAxisAlpha = 1.0f;

    // 水面やシーンのジオメトリとの Z-fighting を避けるため、グリッドをわずかに浮かせる
    static constexpr float kGridPlaneYOffset = 0.01f;
};
}
