#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector3.h"
#include "Sample/TestGameObject/Effect/LightningBoltMeshGenerator.h"

#include <d3d12.h>
#include <wrl/client.h>

class LightningStrikeObject : public CoreEngine::PrimitiveGameObject
    , public CoreEngine::ICustomShaderProvider {
public:
    LightningStrikeObject();

    const char* GetObjectName() const override { return "LightningStrike"; }
    std::wstring GetVertexShaderPath() const override { return L"Lightning.VS.hlsl"; }
    std::wstring GetPixelShaderPath() const override { return L"Lightning.PS.hlsl"; }

    void BindCustomResources(
        ID3D12GraphicsCommandList* cmdList,
        const CoreEngine::CustomShaderPipeline* pipeline) const override;

    void SetStrikeEndpoints(const CoreEngine::Vector3& start, const CoreEngine::Vector3& end);
    void Trigger(float duration = 0.22f, float intensity = 1.0f);
    void Stop();

    void SetNoiseParameters(float tiling, float scrollSpeed);
    void SetGlowShape(float coreExponent, float haloExponent, float fadePower);
    void SetWidthScale(float widthScale);

    bool IsPlaying() const { return isPlaying_; }
    float GetNormalizedAge() const;
    const CoreEngine::Vector3& GetStrikeStart() const { return strikeStart_; }
    const CoreEngine::Vector3& GetStrikeEnd() const { return strikeEnd_; }

protected:
    std::string GetTexturePath() const override { return {}; }
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;
    void OnInitialize() override;
    void OnUpdate() override;

private:
    struct LightningConstants {
        float elapsedTime = 0.0f;
        float duration = 0.22f;
        float intensity = 1.0f;
        float active = 0.0f;

        float noiseTiling = 18.0f;
        float noiseScrollSpeed = 3.5f;
        float coreExponent = 7.5f;
        float haloExponent = 1.8f;

        float fadePower = 2.4f;
        float widthScale = 1.0f;
        float distortionAmplitude = 0.06f;
        float branchPulse = 1.0f;

        float shapeSeed0 = 0.13f;
        float shapeSeed1 = 0.41f;
        float shapeSeed2 = 0.67f;
        float shapeSeed3 = 0.89f;
    };

    void CreateConstantBuffer(ID3D12Device* device);
    void UploadConstants();
    void UpdateTransformFromEndpoints();
    void RebuildBoltMesh();
    void ApplyMaterialSettings();

    LightningBoltMeshGenerator::Settings meshSettings_{};
    CoreEngine::Vector3 strikeStart_ = { 0.0f, 12.0f, 0.0f };
    CoreEngine::Vector3 strikeEnd_ = { 0.0f, 0.0f, 0.0f };

    LightningConstants lightningCB_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> lightningCBResource_;
    D3D12_GPU_VIRTUAL_ADDRESS lightningCBGpuAddress_ = 0;
    uint8_t* lightningCBMapped_ = nullptr;

    bool isPlaying_ = false;
};
