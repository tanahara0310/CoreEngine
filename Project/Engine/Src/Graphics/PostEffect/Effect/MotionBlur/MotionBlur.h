#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h" // PostEffectPassContext
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief モーションブラー（McGuire 方式・タイルベース）
/// @details 実カメラはシャッターが開いている間の光を積分するため、動くものは必ずブレる。
///          これが無い映像は 1 フレームごとに完全静止した「紙芝居」に見える。
///          TileMax（タイル最大速度）→ NeighborMax（隣接 3x3 の max）→ Gather（本体）の
///          3 パス構成。速度は G-Buffer MotionVector、前後判定はシーン深度を使う。
///
///          【既知の制限】MotionVector は G-Buffer を書くジオメトリだけが持つ。
///          空（大気）と水面はフォワード描画で MV=0 のため自身の動きではブレない
///          （手前のジオメトリのブラーが上に被さる分は正しく出る）。
///
///          パラメータは CVar（"r.MotionBlur.*"）が唯一の保持者。
///          設計: Docs/Engine/Graphics/PostProcess/Cinematic_PostEffect_Plan.md
class MotionBlur : public PostEffectComputeBase {
public:
    /// @brief タイル一辺[px]。ブラー最大距離の上限でもある
    /// @details NeighborMax が保証するのは「隣接タイルまで」の到達なので、
    ///          これを超えるブラー距離は探索範囲外になり破綻する
    static constexpr uint32_t kTileSize = 20;

    /// @brief TileMax パスの定数（GPU レイアウト）
    struct TileMaxParams {
        uint32_t screenSize[2]   = { 1, 1 };
        uint32_t tileCount[2]    = { 1, 1 };
        float    shutterFraction = 0.5f;
        float    maxBlurPixels   = 20.0f;
        uint32_t tileSize        = kTileSize;
        float    padding         = 0.0f;
    };

    /// @brief NeighborMax パスの定数（GPU レイアウト）
    struct NeighborMaxParams {
        uint32_t tileCount[2] = { 1, 1 };
        float    padding[2]   = {};
    };

    /// @brief ギャザーパスの定数（GPU レイアウト）
    struct GatherParams {
        uint32_t screenSize[2]   = { 1, 1 };
        uint32_t tileCount[2]    = { 1, 1 };
        float    shutterFraction = 0.5f;
        float    maxBlurPixels   = 20.0f;
        uint32_t sampleCount     = 12;
        uint32_t tileSize        = kTileSize;
        float    nearPlane       = 0.1f;
        float    farPlane        = 1000.0f;
        float    depthExtent     = 1.0f;
        float    padding         = 0.0f;
    };

public:
    MotionBlur() = default;
    ~MotionBlur() = default;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 露光中の積分はトーンカーブを通る前の物理量に対して起きるため SceneHDR 段
    PostEffectStage GetStage() const override { return PostEffectStage::SceneHDR; }

    /// @brief 深度線形化に使う near/far を描画ビューから取り込む
    void PrepareFrame(const PostEffectFrameContext& ctx) override;

    /// @brief 速度と深度が要る。G-Buffer を書かないビューではチェーンから外れる
    void DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const override;

    /// @brief TileMax → NeighborMax → Gather をグラフへ積む
    void BuildPasses(PostEffectGraphBuilder& builder) override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "MotionBlur"; }
    /// @note 基底が要求する 1 本目の CS。ギャザー本体として使う
    std::wstring GetComputeShaderPath() const override { return L"MotionBlur.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    /// @brief TileMax / NeighborMax 用の追加パイプラインを構築する
    bool CreateInternalPipelines();

    void RecordTileMax(const PostEffectPassContext& context);
    void RecordNeighborMax(const PostEffectPassContext& context);
    void RecordGather(const PostEffectPassContext& context);

    /// @brief シェーダーパスだけを差し替える最小のプロバイダ
    class ShaderProvider : public ICustomShaderProvider {
    public:
        explicit ShaderProvider(std::wstring path) : path_(std::move(path)) {}
        std::wstring GetComputeShaderPath() const override { return path_; }
    private:
        std::wstring path_;
    };

    ShaderProvider tileMaxProvider_{ L"MotionBlurTileMax.CS.hlsl" };
    ShaderProvider neighborMaxProvider_{ L"MotionBlurNeighborMax.CS.hlsl" };
    CustomShaderPipeline tileMaxPipeline_;
    CustomShaderPipeline neighborMaxPipeline_;
    bool internalPipelinesReady_ = false;

    // 定数バッファはパスごとに別実体が要る（GPU が読むのは記録より後なので使い回せない）
    Microsoft::WRL::ComPtr<ID3D12Resource> tileMaxParamsCB_;
    TileMaxParams* mappedTileMaxParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> neighborMaxParamsCB_;
    NeighborMaxParams* mappedNeighborMaxParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> gatherParamsCB_;
    GatherParams* mappedGatherParams_ = nullptr;

    /// @brief BuildPasses が確定させたフル解像度（record から参照する）
    uint32_t baseWidth_  = 0;
    uint32_t baseHeight_ = 0;

    /// @brief タイルバッファの実寸法。RecordTileMax が出力ターゲットの実サイズで確定させ、
    ///        後続の RecordNeighborMax / RecordGather が参照する（グラフの依存順で保証される）
    uint32_t tileCountX_ = 0;
    uint32_t tileCountY_ = 0;

    /// @brief 深度線形化用のクリップ距離（PrepareFrame が描画ビューから更新する）
    float nearPlane_ = 0.1f;
    float farPlane_  = 1000.0f;
};
}
