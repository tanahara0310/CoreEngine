#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h" // PostEffectPassContext
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief ローカル露出（局所トーンマッピング）
/// @details グローバル露出では「明るい空」と「影の地形・水面」を同時に見せられない。
///          対数輝度をベース層（1/8 解像度の大きなガウシアン）とディテール層に分離し、
///          ベース層だけを中間グレーへ向けて圧縮する。UE5 の Local Exposure と同じ考え方。
///          ハロ対策として、適用時にフル解像度輝度をガイドにした
///          ジョイントバイラテラルアップサンプルでベース層を再構成する。
///
///          Downsample(1/8 logLum) → BlurH → BlurV → Apply の 4 パス構成。
///          パラメータは CVar（"r.LocalExposure.*"）が唯一の保持者。
///          設計: Docs/Engine/Graphics/PostProcess/Cinematic_PostEffect_Plan.md
class LocalExposure : public PostEffectComputeBase {
public:
    /// @brief ベース層の縮小率（1/8 解像度）
    static constexpr uint32_t kBaseDivisor = 8;

    /// @brief ダウンサンプルパスの定数（GPU レイアウト）
    struct DownsampleParams {
        uint32_t outputSize[2] = { 1, 1 };
        uint32_t sourceSize[2] = { 1, 1 };
    };

    /// @brief ブラーパスの定数（GPU レイアウト）
    struct BlurParams {
        uint32_t textureSize[2] = { 1, 1 };
        uint32_t direction[2]   = { 1, 0 };
    };

    /// @brief 適用パスの定数（GPU レイアウト）
    struct ApplyParams {
        uint32_t screenSize[2]     = { 1, 1 };
        uint32_t baseSize[2]       = { 1, 1 };
        float    highlightContrast = 0.8f;
        float    shadowContrast    = 0.9f;
        float    detailStrength    = 1.0f;
        float    middleGreyBias    = 0.0f;
        float    rangeSigma        = 1.0f;
        float    padding[3]        = {};
    };

public:
    LocalExposure() = default;
    ~LocalExposure() = default;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 露出の一種なのでトーンカーブ前の物理量に対して効かせる SceneHDR 段
    PostEffectStage GetStage() const override { return PostEffectStage::SceneHDR; }

    /// @brief Downsample → BlurH → BlurV → Apply をグラフへ積む
    void BuildPasses(PostEffectGraphBuilder& builder) override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "LocalExposure"; }
    /// @note 基底が要求する 1 本目の CS。適用パスとして使う
    std::wstring GetComputeShaderPath() const override { return L"LocalExposure.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    /// @brief Downsample / Blur 用の追加パイプラインを構築する
    bool CreateInternalPipelines();

    void RecordDownsample(const PostEffectPassContext& context);
    void RecordBlur(const PostEffectPassContext& context, bool horizontal);
    void RecordApply(const PostEffectPassContext& context);

    /// @brief シェーダーパスだけを差し替える最小のプロバイダ
    class ShaderProvider : public ICustomShaderProvider {
    public:
        explicit ShaderProvider(std::wstring path) : path_(std::move(path)) {}
        std::wstring GetComputeShaderPath() const override { return path_; }
    private:
        std::wstring path_;
    };

    ShaderProvider downsampleProvider_{ L"LocalExposureDownsample.CS.hlsl" };
    ShaderProvider blurProvider_{ L"LocalExposureBlur.CS.hlsl" };
    CustomShaderPipeline downsamplePipeline_;
    CustomShaderPipeline blurPipeline_;
    bool internalPipelinesReady_ = false;

    // 定数バッファはパスごとに別実体が要る（GPU が読むのは記録より後なので使い回せない）
    Microsoft::WRL::ComPtr<ID3D12Resource> downsampleParamsCB_;
    DownsampleParams* mappedDownsampleParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> blurHParamsCB_;
    BlurParams* mappedBlurHParams_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurVParamsCB_;
    BlurParams* mappedBlurVParams_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> applyParamsCB_;
    ApplyParams* mappedApplyParams_ = nullptr;

    /// @brief BuildPasses が確定させた解像度（record から参照する）
    uint32_t baseWidth_  = 0;
    uint32_t baseHeight_ = 0;

    /// @brief ベース層の実寸法。RecordDownsample が出力ターゲットの実サイズで確定させる
    uint32_t lowResWidth_  = 0;
    uint32_t lowResHeight_ = 0;
};
}
