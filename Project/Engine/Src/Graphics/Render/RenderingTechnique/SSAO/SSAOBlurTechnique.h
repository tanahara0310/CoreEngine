#pragma once
#include "../RenderingTechniqueBase.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
/// @brief SSAOブラー レンダリング技術
/// @details SSAOの出力にバイラテラルブラーをかけてノイズを除去
class SSAOBlurTechnique : public RenderingTechniqueBase {
public:
    struct SSAOBlurParams {
        float screenWidth   = 1280.0f;
        float screenHeight  = 720.0f;
        float depthThreshold = 0.5f;
        float _pad0          = 0.0f;
    };

public:
    SSAOBlurTechnique() = default;
    ~SSAOBlurTechnique() = default;

    void Initialize(DirectXCommon* dxCommon) override;
    void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
    void OnResize(uint32_t width, uint32_t height) override;

    const SSAOBlurParams& GetParams() const { return params_; }
    void SetParams(const SSAOBlurParams& params);
    void UpdateConstantBuffer();

protected:
    std::string GetTechniqueName() const override { return "SSAOBlur"; }
    const std::wstring& GetPixelShaderPath() const override;

private:
    void CreateConstantBuffer();

    SSAOBlurParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    SSAOBlurParams* mappedData_ = nullptr;
};
}
