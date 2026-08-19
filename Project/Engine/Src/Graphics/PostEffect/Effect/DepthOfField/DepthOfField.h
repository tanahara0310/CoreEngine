#pragma once

#include "../PostEffectComputeBase.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h" // PostEffectPassContext
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief 被写界深度（DoF・薄レンズモデル）
/// @details パラメータは実カメラ単位（焦点距離[m]・F 値・レンズ焦点距離[mm]／フルサイズ 36mm 前提）。
///          Prefilter → Gather → Composite の 3 パス。CVar "r.DoF.*"。既定 OFF。
/// @note 単層ギャザーのため、強い前ボケの半透明な輪郭は物理的に正確ではない。
class DepthOfField : public PostEffectComputeBase {
public:
    /// @brief Prefilter パスの定数（GPU レイアウト）
    struct PrefilterParams {
        uint32_t outputSize[2] = { 1, 1 };
        uint32_t fullSize[2]   = { 1, 1 };
        float    focusDistance = 20.0f;
        float    cocScalePx    = 0.0f;
        float    maxCocPx      = 16.0f;
        float    nearPlane     = 0.1f;
        float    farPlane      = 1000.0f;
        float    padding[3]    = {};
    };

    static constexpr Cb::Field kPrefilterParamsFields[] = {
        CB_FIELD(PrefilterParams, outputSize), CB_FIELD(PrefilterParams, fullSize),
        CB_FIELD(PrefilterParams, focusDistance), CB_FIELD(PrefilterParams, cocScalePx),
        CB_FIELD(PrefilterParams, maxCocPx), CB_FIELD(PrefilterParams, nearPlane),
        CB_FIELD(PrefilterParams, farPlane), CB_FIELD(PrefilterParams, padding),
    };
    CB_VERIFY_LAYOUT(PrefilterParams, kPrefilterParamsFields);
    CB_BIND_HLSL(PrefilterParams, kPrefilterParamsFields, "DoFPrefilterParams");

    /// @brief Gather パスの定数（GPU レイアウト）
    struct GatherParams {
        uint32_t textureSize[2] = { 1, 1 };
        float    maxCocHalfPx   = 8.0f;
        uint32_t sampleCount    = 48;
    };

    static constexpr Cb::Field kDofGatherParamsFields[] = {
        CB_FIELD(GatherParams, textureSize), CB_FIELD(GatherParams, maxCocHalfPx),
        CB_FIELD(GatherParams, sampleCount),
    };
    CB_VERIFY_LAYOUT(GatherParams, kDofGatherParamsFields);
    CB_BIND_HLSL(GatherParams, kDofGatherParamsFields, "DoFGatherParams");

    /// @brief Composite パスの定数（GPU レイアウト）
    struct CompositeParams {
        uint32_t fullSize[2]   = { 1, 1 };
        uint32_t halfSize[2]   = { 1, 1 };
        float    focusDistance = 20.0f;
        float    cocScalePx    = 0.0f;
        float    maxCocPx      = 16.0f;
        float    nearPlane     = 0.1f;
        float    farPlane      = 1000.0f;
        float    padding[3]    = {};
    };

    static constexpr Cb::Field kDofCompositeParamsFields[] = {
        CB_FIELD(CompositeParams, fullSize), CB_FIELD(CompositeParams, halfSize),
        CB_FIELD(CompositeParams, focusDistance), CB_FIELD(CompositeParams, cocScalePx),
        CB_FIELD(CompositeParams, maxCocPx), CB_FIELD(CompositeParams, nearPlane),
        CB_FIELD(CompositeParams, farPlane), CB_FIELD(CompositeParams, padding),
    };
    CB_VERIFY_LAYOUT(CompositeParams, kDofCompositeParamsFields);
    CB_BIND_HLSL(CompositeParams, kDofCompositeParamsFields, "DoFCompositeParams");

public:
    DepthOfField() = default;
    ~DepthOfField() = default;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief レンズの光学現象なのでトーンカーブ前の物理量に対して効かせる SceneHDR 段
    PostEffectStage GetStage() const override { return PostEffectStage::SceneHDR; }

    /// @brief 深度線形化に使う near/far を描画ビューから取り込む
    void PrepareFrame(const PostEffectFrameContext& ctx) override;

    /// @brief 深度が要る。G-Buffer を書かないビューではチェーンから外れる
    void DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const override;

    /// @brief Prefilter → Gather → Composite をグラフへ積む
    void BuildPasses(PostEffectGraphBuilder& builder) override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "DepthOfField"; }
    /// @note 基底が要求する 1 本目の CS。合成パスとして使う
    std::wstring GetComputeShaderPath() const override { return L"DoFComposite.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    /// @brief Prefilter / Gather 用の追加パイプラインを構築する
    bool CreateInternalPipelines();

    /// @brief 薄レンズ式の CoC 係数[フル解像度px]を CVar から計算する
    float ComputeCocScalePx() const;

    void RecordPrefilter(const PostEffectPassContext& context);
    void RecordGather(const PostEffectPassContext& context);
    void RecordComposite(const PostEffectPassContext& context);

    /// @brief シェーダーパスだけを差し替える最小のプロバイダ
    class ShaderProvider : public ICustomShaderProvider {
    public:
        explicit ShaderProvider(std::wstring path) : path_(std::move(path)) {}
        std::wstring GetComputeShaderPath() const override { return path_; }
    private:
        std::wstring path_;
    };

    ShaderProvider prefilterProvider_{ L"DoFPrefilter.CS.hlsl" };
    ShaderProvider gatherProvider_{ L"DoFGather.CS.hlsl" };
    CustomShaderPipeline prefilterPipeline_;
    CustomShaderPipeline gatherPipeline_;
    bool internalPipelinesReady_ = false;

    // 定数バッファはパスごとに別実体が要る（GPU が読むのは記録より後なので使い回せない）
    Microsoft::WRL::ComPtr<ID3D12Resource> prefilterParamsCB_;
    PrefilterParams* mappedPrefilterParams_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> gatherParamsCB_;
    GatherParams* mappedGatherParams_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> compositeParamsCB_;
    CompositeParams* mappedCompositeParams_ = nullptr;

    /// @brief BuildPasses が確定させた解像度（record から参照する）
    uint32_t fullWidth_  = 0;
    uint32_t fullHeight_ = 0;
    uint32_t halfWidth_  = 0;
    uint32_t halfHeight_ = 0;

    /// @brief 深度線形化用のクリップ距離（PrepareFrame が描画ビューから更新する）
    float nearPlane_ = 0.1f;
    float farPlane_  = 1000.0f;
};
}
