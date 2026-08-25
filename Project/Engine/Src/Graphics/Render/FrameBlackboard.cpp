#include "pch.h"
#include "FrameBlackboard.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

namespace CoreEngine
{
    const char* const FrameBlackboard::SceneColor = "SceneColor";
    const char* const FrameBlackboard::SceneColorSnapshot = "SceneColorSnapshot";
    const char* const FrameBlackboard::SceneDepth = "SceneDepth";
    const char* const FrameBlackboard::ReflectionColor = "ReflectionColor";
    const char* const FrameBlackboard::ReflectionDepth = "ReflectionDepth";
    const char* const FrameBlackboard::SSAO = "SSAO";
    const char* const FrameBlackboard::ShadowMask = "ShadowMask";
    const char* const FrameBlackboard::RTShadowMask = "RTShadowMask";
    const char* const FrameBlackboard::WaterCaustics = "WaterCaustics";
    const char* const FrameBlackboard::RTWaterCaustics = "RTWaterCaustics";
    const char* const FrameBlackboard::RTWaterRefractionColor = "RTWaterRefractionColor";
    const char* const FrameBlackboard::RTWaterReflectionColor = "RTWaterReflectionColor";
    const char* const FrameBlackboard::BackBuffer = "BackBuffer";
    const char* const FrameBlackboard::PostEffectFinal = "PostEffectFinal";
    const char* const FrameBlackboard::GBufferAlbedoAO = "GBufferAlbedoAO";
    const char* const FrameBlackboard::GBufferNormalRoughness = "GBufferNormalRoughness";
    const char* const FrameBlackboard::GBufferEmissiveMetallic = "GBufferEmissiveMetallic";
    const char* const FrameBlackboard::GBufferMotionVector = "GBufferMotionVector";
    const char* const FrameBlackboard::TAAHistory = "TAAHistory";
    const char* const FrameBlackboard::TAAOutput = "TAAOutput";
    const char* const FrameBlackboard::CASOutput = "CASOutput";

    std::string FrameBlackboard::MakePostEffectIntermediateName(size_t index)
    {
        // 接頭辞の実体は RenderTargetNames が唯一持つ。ここで文字列リテラルを再掲すると、
        // 名前を変えたときに論理名と物理ターゲット名が静かに食い違う。
        return std::string(RenderTargetNames::PostEffectIntermediatePrefix) + std::to_string(index);
    }

    void FrameBlackboardResource::Reset()
    {
        srvHandle = {};
        resource = nullptr;
        isValid = false;
    }

    void FrameBlackboard::Reset()
    {
        resources_.clear();
    }

    void FrameBlackboard::SetResource(
        const std::string& name,
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle,
        GpuResource* resource)
    {
        FrameBlackboardResource& entry = resources_[name];
        entry.name = name;
        entry.srvHandle = srvHandle;
        entry.resource = resource;
        entry.isValid = (srvHandle.ptr != 0) || (resource != nullptr && resource->IsValid());
    }

    void FrameBlackboard::InvalidateResource(const std::string& name)
    {
        FrameBlackboardResource& entry = resources_[name];
        entry.name = name;
        entry.Reset();
    }

    const FrameBlackboardResource* FrameBlackboard::GetResource(const std::string& name) const
    {
        auto it = resources_.find(name);
        if (it == resources_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    bool FrameBlackboard::TryGetSrvHandle(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE& outHandle) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        if (!entry || !entry->isValid || entry->srvHandle.ptr == 0) {
            return false;
        }
        outHandle = entry->srvHandle;
        return true;
    }

    bool FrameBlackboard::HasResource(const std::string& name) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        return entry && entry->isValid;
    }

    bool FrameBlackboard::TryResolveResource(
        const std::string& name,
        GpuResource*& outResource) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        if (!entry || !entry->isValid || !entry->resource || !entry->resource->IsValid()) {
            return false;
        }

        outResource = entry->resource;
        return true;
    }
}
