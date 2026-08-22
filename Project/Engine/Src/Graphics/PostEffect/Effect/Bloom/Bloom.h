#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h" // PostEffectPassContext
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>
#include <array>


namespace CoreEngine
{
/// @brief ブルームエフェクト（ミップチェーン方式）
/// @details ダウンサンプル 6 段 → アップサンプル 5 段 → 合成。1/64 解像度まで畳んで戻すので
///          画面の 1/2 に届く広がりが出る。CVar "r.Bloom.*"。
class Bloom : public PostEffectComputeBase {
public:
    /// @brief ミップチェーンの段数。1/2 から 1/64 まで
    static constexpr int kMipCount = 6;

    /// @brief ダウンサンプル 1 段分の定数（GPU レイアウト）
    struct DownsampleParams {
        uint32_t outputSize[2]   = { 1, 1 };
        uint32_t sourceSize[2]   = { 1, 1 };
        float    threshold       = 0.8f;
        float    softKnee        = 0.5f;
        uint32_t applyPrefilter  = 0;
        float    padding         = 0.0f;
    };

    static constexpr Cb::Field kBloomDownsampleParamsFields[] = {
        CB_FIELD(DownsampleParams, outputSize), CB_FIELD(DownsampleParams, sourceSize),
        CB_FIELD(DownsampleParams, threshold), CB_FIELD(DownsampleParams, softKnee),
        CB_FIELD(DownsampleParams, applyPrefilter), CB_FIELD(DownsampleParams, padding),
    };
    CB_VERIFY_LAYOUT(DownsampleParams, kBloomDownsampleParamsFields);
    CB_BIND_HLSL(DownsampleParams, kBloomDownsampleParamsFields, "BloomDownsampleParams");

    /// @brief アップサンプル 1 段分の定数（GPU レイアウト）
    struct UpsampleParams {
        uint32_t outputSize[2] = { 1, 1 };
        uint32_t lowerSize[2]  = { 1, 1 };
    };

    static constexpr Cb::Field kBloomUpsampleParamsFields[] = {
        CB_FIELD(UpsampleParams, outputSize), CB_FIELD(UpsampleParams, lowerSize),
    };
    CB_VERIFY_LAYOUT(UpsampleParams, kBloomUpsampleParamsFields);
    CB_BIND_HLSL(UpsampleParams, kBloomUpsampleParamsFields, "BloomUpsampleParams");

    /// @brief 合成パスの定数（GPU レイアウト）
    struct CompositeParams {
        uint32_t outputSize[2] = { 1, 1 };
        uint32_t bloomSize[2]  = { 1, 1 };
        float    intensity     = 1.0f;
        float    dirtIntensity = 0.0f;
        uint32_t dirtSize      = 512;
        float    padding       = 0.0f;
    };

    static constexpr Cb::Field kBloomCompositeParamsFields[] = {
        CB_FIELD(CompositeParams, outputSize), CB_FIELD(CompositeParams, bloomSize),
        CB_FIELD(CompositeParams, intensity), CB_FIELD(CompositeParams, dirtIntensity),
        CB_FIELD(CompositeParams, dirtSize), CB_FIELD(CompositeParams, padding),
    };
    CB_VERIFY_LAYOUT(CompositeParams, kBloomCompositeParamsFields);
    CB_BIND_HLSL(CompositeParams, kBloomCompositeParamsFields, "BloomCompositeParams");

    /// @brief ダート生成パスの定数（GPU レイアウト）
    struct DirtGenParams {
        uint32_t textureSize = 512;
        float    padding[3]  = {};
    };

    static constexpr Cb::Field kBloomDirtGenParamsFields[] = {
        CB_FIELD(DirtGenParams, textureSize), CB_FIELD(DirtGenParams, padding),
    };
    CB_VERIFY_LAYOUT(DirtGenParams, kBloomDirtGenParamsFields);
    CB_BIND_HLSL(DirtGenParams, kBloomDirtGenParamsFields, "DirtGenParams");

    /// @brief ダートマスクの一辺
    static constexpr uint32_t kDirtTextureSize = 512;

public:
    Bloom() = default;
    ~Bloom() = default;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 光の散乱はトーンマップ前の物理量に対して起きるため SceneHDR 段
    PostEffectStage GetStage() const override { return PostEffectStage::SceneHDR; }

    /// @brief ダウン 6 段 → アップ 5 段 → 合成 をグラフへ積む
    void BuildPasses(PostEffectGraphBuilder& builder) override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "Bloom"; }
    /// @note 基底が要求する 1 本目の CS。合成パスとして使う
    std::wstring GetComputeShaderPath() const override { return L"BloomComposite.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    /// @brief ダウンサンプル／アップサンプル用の追加パイプラインを構築する
    bool CreateInternalPipelines();

    /// @brief 1 パス分の記録処理（ダウンサンプル）
    void RecordDownsample(const PostEffectPassContext& context, int mipIndex);
    /// @brief 1 パス分の記録処理（アップサンプル）
    void RecordUpsample(const PostEffectPassContext& context, int upIndex);
    /// @brief 1 パス分の記録処理（合成）
    void RecordComposite(const PostEffectPassContext& context);

    /// @brief シェーダーパスだけを差し替える最小のプロバイダ
    class ShaderProvider : public ICustomShaderProvider {
    public:
        explicit ShaderProvider(std::wstring path) : path_(std::move(path)) {}
        std::wstring GetComputeShaderPath() const override { return path_; }
    private:
        std::wstring path_;
    };

    ShaderProvider downsampleProvider_{ L"BloomDownsample.CS.hlsl" };
    ShaderProvider upsampleProvider_{ L"BloomUpsample.CS.hlsl" };
    ShaderProvider dirtGenProvider_{ L"BloomDirtGen.CS.hlsl" };
    CustomShaderPipeline downsamplePipeline_;
    CustomShaderPipeline upsamplePipeline_;
    CustomShaderPipeline dirtGenPipeline_;
    bool internalPipelinesReady_ = false;

    /// @brief レンズダートマスク（起動後の初回合成時に CS で手続き生成する）
    bool CreateDirtResources();
    void RecordDirtGenerationIfNeeded(ID3D12GraphicsCommandList* cmdList);
    Microsoft::WRL::ComPtr<ID3D12Resource> dirtTexture_;
    D3D12_RESOURCE_STATES dirtTextureState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    DescriptorHandle dirtSrvHandle_{};
    DescriptorHandle dirtUavHandle_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> dirtGenParamsCB_;
    DirtGenParams* mappedDirtGenParams_ = nullptr;
    bool dirtResourcesReady_ = false;
    bool dirtGenerated_ = false;

    // 定数バッファはパスごとに別実体が要る（GPU が読むのは記録より後なので使い回せない）
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMipCount> downParamsCB_;
    std::array<DownsampleParams*, kMipCount> mappedDownParams_{};

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMipCount - 1> upParamsCB_;
    std::array<UpsampleParams*, kMipCount - 1> mappedUpParams_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> compositeParamsCB_;
    CompositeParams* mappedCompositeParams_ = nullptr;

    /// @brief BuildPasses が計算した各段の解像度（record から参照する）
    std::array<uint32_t, kMipCount> mipWidth_{};
    std::array<uint32_t, kMipCount> mipHeight_{};
    uint32_t baseWidth_ = 0;
    uint32_t baseHeight_ = 0;
};
}
