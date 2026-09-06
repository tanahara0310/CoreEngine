#pragma once

#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/Render/Line/ILineSource.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <d3d12.h>
#include <string>

namespace CoreEngine
{
class EngineSystem;
class GraphicsCore;

/// @brief XZ 平面へ広がるエディタ用グリッド（Blender / 商用エンジンと同じ解析グリッド）
/// @details 線を 1 本ずつ引くのではなく、画面全体を覆う三角形 1 枚をピクセルシェーダーで
///          評価する。ピクセルごとにカメラレイと床平面の交点を求め、そこで格子模様を
///          解析的に出す。ラインプリミティブをやめたことで次の 3 つが同時に解決する。
///          - 線の濃さが「線までの距離 ÷ 1 ピクセルの足跡」で決まるので常にアンチエイリアスされる
///            （固定機能のライン描画はカバレッジが 0 か 1 しかなく、必ずジャギーになる）
///          - 段（10 倍ごとの粗さ）を足跡の大きさで選ぶ。ミップマップと同じ理屈で、
///            画面上で格子が詰まりすぎないためモアレ（線同士のじりじり）が出ない
///          - 距離で α を落とす必要がないので、遠くの線が薄れて消えることがない
/// @note 垂直な Y 軸だけは床平面に乗らず解析グリッドでは描けないため、
///       従来どおり Line パスへ 1 本流す（ILineSource としての役割）。
class GridRenderer : public BaseRenderer, public ILineSource {
public:
    /// @brief シェーダーの GridParams と 1 対 1 で対応する定数バッファ
    /// @note Assets/Shaders/Include/Grid/GridCommon.hlsli と並びを合わせること
    /// @note 行列は「カメラ相対」であること。絶対ワールド座標のまま交点を求めると、
    ///       カメラが原点から離れたときに float32 の桁が足りず、交点とその画面スペース
    ///       微分がピクセル単位でばらついて一面ノイズになる。
    struct GridConstants {
        Matrix4x4 viewProjection;      // カメラ相対ワールド → クリップ
        Matrix4x4 invViewProjection;   // クリップ → カメラ相対ワールド
        Vector3 cameraPosition; float planeY;
        Vector3 gridColor;      float baseSpacing;
        Vector3 xAxisColor;     float brightness;
        Vector3 zAxisColor;     float minPixelsPerCell;
        float maxLevel;
        float lineWidthPixels;
        float axisAlpha;
        float depthBias;
    };

    // ===== IRenderer =====
    void Initialize(ID3D12Device* device) override;
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
    void EndPass() override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::Grid; }
    void SetCamera(const Camera* camera) override;

    /// @brief GraphicsCore 付きの初期化
    /// @details 定数バッファをフラッシュのたびに UploadRing から取るため、こちらを使うこと。
    ///          Line パスと同じく 1 フレーム中にビューの数だけ実行されるので、
    ///          専用リソースを 1 本使い回すと GPU 実行前に上書きしてしまう。
    void Initialize(GraphicsCore* dxCommon);

    // ===== ILineSource =====
    /// @brief 垂直な Y 軸（原点の目印）だけを Line パスへ供給する
    void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) override;

    /// @brief グリッドの表示/非表示を設定
    /// @note 既定は非表示。エディタの GridFeature が有効にする
    void SetVisible(bool visible) { visible_ = visible; }

    /// @brief 表示中かどうか
    bool IsVisible() const { return visible_; }

    /// @brief 最も細かい格子の間隔 [m]（これより細かい段は出さない）
    void SetBaseSpacing(float spacing);

    /// @brief 1 マスが画面上で保つ最小ピクセル数
    /// @details 段の切り替え基準。小さいほど細かい格子が遠くまで残るが、
    ///          詰まりすぎるとモアレの手前で薄くなっていく。
    void SetMinPixelsPerCell(float pixels);

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;

#ifdef USE_IMGUI
    /// @brief Engine Settings に「Grid」パネルを登録する（プロセスで一度だけ）
    /// @details パネルはファイルスコープの「現在アクティブなグリッド」を読むだけで
    ///          何もキャプチャしない（GameDebugUI に登録解除 API が無いため）。
    static void EnsureSettingsPanelRegistered(EngineSystem* engine);

    /// @brief このグリッドをパネルの編集対象にする（nullptr で解除）
    static void SetActiveForSettingsPanel(GridRenderer* grid);
#endif

private:
    /// @brief 画面全体を覆う三角形 1 枚を描く
    void DrawGrid();

#ifdef USE_IMGUI
    /// @brief 設定パネルの中身を描画する
    bool DrawSettingsImGui();
#endif

    GraphicsCore* dxCommon_ = nullptr;
    const Camera* camera_ = nullptr;
    ID3D12GraphicsCommandList* currentCmdList_ = nullptr;

    bool visible_ = false;              // 表示フラグ（GridFeature が立てる）
    float baseSpacing_ = 1.0f;          // 最細の格子間隔 [m]
    float minPixelsPerCell_ = 20.0f;    // 1 マスの最小ピクセル数（段の選択基準）
    float lineWidthPixels_ = 1.0f;      // 線の太さ [px]
    float brightness_ = 1.0f;           // 全体の濃さ（α への一括係数）

    // Blender 風のカラー設定
    Vector3 xAxisColor_ = { 0.85f, 0.0f, 0.0f };   // X軸の色（赤）
    Vector3 yAxisColor_ = { 0.0f, 0.0f, 0.85f };   // Y軸の色（青）
    Vector3 zAxisColor_ = { 0.0f, 0.85f, 0.0f };   // Z軸の色（緑）
    Vector3 normalColor_ = { 0.4f, 0.35f, 0.25f }; // 通常のグリッド色（オレンジっぽいグレー）

    /// baseSpacing_ から何段まで粗くしてよいか（10^kMaxLevel 倍まで）。
    /// これを超える＝一番粗い段でも画面上で解像できない領域なので、そこは消す。
    /// 実際にはほぼ地平線の数ピクセルだけが該当する。
    static constexpr float kMaxLevel = 5.0f;

    /// 軸ライン透明度
    static constexpr float kAxisAlpha = 1.0f;

    // 水面やシーンのジオメトリとの Z-fighting を避けるため、床平面をわずかに浮かせる
    static constexpr float kGridPlaneYOffset = 0.01f;

    /// 深度を手前へ寄せる量（NDC）。遠方では深度バッファの量子化幅が上の 1cm を
    /// 上回るため、平面の浮かせだけでは前後が入れ替わってちらつく。Line.VS.hlsl と同じ根拠で、
    /// D24_UNORM の量子化幅 1/2^24 の 150 倍ほどを取っている。
    static constexpr float kDepthBias = 1.0e-5f;

    /// Y 軸（青い柱）の長さのカメラ高度に対する倍率。
    /// 高度に比例させることで、どの高さでも画面上の見え方が変わらない。
    static constexpr float kYAxisHeightScale = 5.0f;
};
}
