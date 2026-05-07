#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
class SSAO : public PostEffectBase {
public:
    struct SSAOParams {
        float viewMatrix[16]       = {};
        float projectionMatrix[16] = {};
        float radius      = 0.5f;
        float bias        = 0.025f;
        float intensity   = 1.0f;
        int   sampleCount = 16;
        float screenWidth  = 1280.0f;
        float screenHeight = 720.0f;
        float power  = 1.5f;
        float _pad0  = 0.0f;
    };

public:
    SSAO() = default;
    ~SSAO() = default;

    void Initialize(DirectXCommon* dxCommon);

    void DrawImGui() override;

    void SetNormalRoughnessHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { normalRoughnessHandle_ = handle; }
    void SetWorldPositionHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { worldPositionHandle_ = handle; }

    void SetViewMatrix(const float mat[16]);
    void SetProjectionMatrix(const float mat[16]);
    void SetScreenSize(float w, float h);

    const SSAOParams& GetParams() const { return params_; }
    void SetParams(const SSAOParams& params);
    void UpdateConstantBuffer();

    bool IsAlwaysEnabled() const override { return false; }

protected:
    const std::wstring& GetPixelShaderPath() const override;
    void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
    void CreateConstantBuffer();

    SSAOParams params_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    SSAOParams* mappedData_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE worldPositionHandle_{};
};
}
