#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
class SSAOBlur : public PostEffectBase {
public:
    struct SSAOBlurParams {
        float screenWidth   = 1280.0f;
        float screenHeight  = 720.0f;
        float depthThreshold = 0.5f;
        float _pad0          = 0.0f;
    };

public:
    SSAOBlur() = default;
    ~SSAOBlur() = default;

    void Initialize(DirectXCommon* dxCommon);

    void SetWorldPositionHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { worldPositionHandle_ = handle; }
    void SetScreenSize(float w, float h);

    const SSAOBlurParams& GetParams() const { return params_; }
    void SetParams(const SSAOBlurParams& params);
    void UpdateConstantBuffer();

    bool IsAlwaysEnabled() const override { return false; }

protected:
    const std::wstring& GetPixelShaderPath() const override;
    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    void CreateConstantBuffer();

    SSAOBlurParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    SSAOBlurParams* mappedData_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE worldPositionHandle_{};
};
}
