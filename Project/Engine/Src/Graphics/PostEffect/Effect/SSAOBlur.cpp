#include "SSAOBlur.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>

namespace CoreEngine
{
    void SSAOBlur::Initialize(DirectXCommon* dxCommon)
    {
        PostEffectBase::Initialize(dxCommon);
        CreateConstantBuffer();
    }

    const std::wstring& SSAOBlur::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAOBlur.PS.hlsl";
        return path;
    }

    void SSAOBlur::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        const int wpIdx = GetRootParamIndex("gWorldPosition");
        if (wpIdx >= 0 && worldPositionHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(wpIdx, worldPositionHandle_);
        }

        const int paramsIdx = GetRootParamIndex("SSAOBlurParams");
        if (paramsIdx >= 0 && constantBuffer_) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }
    }

    void SSAOBlur::SetScreenSize(float w, float h)
    {
        params_.screenWidth  = w;
        params_.screenHeight = h;
    }

    void SSAOBlur::SetParams(const SSAOBlurParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void SSAOBlur::UpdateConstantBuffer()
    {
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void SSAOBlur::CreateConstantBuffer()
    {
        assert(directXCommon_);
        const UINT bufferSize = (sizeof(SSAOBlurParams) + 255) & ~255;
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }
}
