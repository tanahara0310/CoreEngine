#pragma once
#include "../RenderingTechniqueBase.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief SSAO (Screen Space Ambient Occlusion) レンダリング技術
    /// @details GBufferから法線・深度・ワールド座標を使用してAOを計算
    class SSAOTechnique : public RenderingTechniqueBase {
    public:
        struct SSAOParams {
            float viewMatrix[16] = {};
            float projectionMatrix[16] = {};
            float radius = 0.5f;
            float bias = 0.025f;
            float intensity = 1.0f;
            int   sampleCount = 16;
            float screenWidth = 1280.0f;
            float screenHeight = 720.0f;
            float power = 1.5f;
            float _pad0 = 0.0f;
        };

    public:
        SSAOTechnique() = default;
        ~SSAOTechnique() = default;

        void Initialize(DirectXCommon* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
        void OnResize(uint32_t width, uint32_t height) override;
        void DrawImGui() override;

        const SSAOParams& GetParams() const { return params_; }
        void SetParams(const SSAOParams& params);
        void UpdateConstantBuffer();

    protected:
        std::string GetTechniqueName() const override { return "SSAO"; }
        const std::wstring& GetPixelShaderPath() const override;

    private:
        void CreateConstantBuffer();

        SSAOParams params_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        SSAOParams* mappedData_ = nullptr;
    };
}
