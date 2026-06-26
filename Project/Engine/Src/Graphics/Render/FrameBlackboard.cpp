#include "pch.h"
#include "FrameBlackboard.h"

namespace CoreEngine
{
    const char* const FrameBlackboard::SceneColor = "SceneColor";
    const char* const FrameBlackboard::SceneDepth = "SceneDepth";
    const char* const FrameBlackboard::ReflectionColor = "ReflectionColor";
    const char* const FrameBlackboard::ReflectionDepth = "ReflectionDepth";
    const char* const FrameBlackboard::SSAO = "SSAO";
    const char* const FrameBlackboard::ShadowMap = "ShadowMap";
    const char* const FrameBlackboard::ShadowMask = "ShadowMask";
    const char* const FrameBlackboard::RTShadowMask = "RTShadowMask";
    const char* const FrameBlackboard::WaterCaustics = "WaterCaustics";
    const char* const FrameBlackboard::RTWaterCaustics = "RTWaterCaustics";
    const char* const FrameBlackboard::RTWaterRefractionColor = "RTWaterRefractionColor";
    const char* const FrameBlackboard::BackBuffer = "BackBuffer";
    const char* const FrameBlackboard::PostEffectFinal = "PostEffectFinal";
    const char* const FrameBlackboard::GBufferAlbedoAO = "GBufferAlbedoAO";
    const char* const FrameBlackboard::GBufferNormalRoughness = "GBufferNormalRoughness";
    const char* const FrameBlackboard::GBufferEmissiveMetallic = "GBufferEmissiveMetallic";
    const char* const FrameBlackboard::GBufferWorldPosition = "GBufferWorldPosition";
    const char* const FrameBlackboard::GBufferMotionVector = "GBufferMotionVector";

    std::string FrameBlackboard::MakePostEffectIntermediateName(size_t index)
    {
        return "PostEffectIntermediate" + std::to_string(index);
    }

    void FrameBlackboardResource::Reset()
    {
        srvHandle = {};
        resource = nullptr;
        currentState = nullptr;
        isValid = false;
    }

    void FrameBlackboard::Reset()
    {
        resources_.clear();
    }

    void FrameBlackboard::SetResource(
        const std::string& name,
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES* currentState)
    {
        FrameBlackboardResource& entry = resources_[name];
        entry.name = name;
        entry.srvHandle = srvHandle;
        entry.resource = resource;
        entry.currentState = currentState;
        entry.isValid = (srvHandle.ptr != 0) || (resource != nullptr);
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

    D3D12_RESOURCE_STATES* FrameBlackboard::GetCurrentStateRef(const std::string& name) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        if (!entry || !entry->isValid) {
            return nullptr;
        }
        return entry->currentState;
    }

    bool FrameBlackboard::HasResource(const std::string& name) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        return entry && entry->isValid;
    }

    bool FrameBlackboard::TryResolveResource(
        const std::string& name,
        ID3D12Resource*& outResource,
        D3D12_RESOURCE_STATES*& outCurrentState) const
    {
        const FrameBlackboardResource* entry = GetResource(name);
        if (!entry || !entry->isValid || !entry->resource || !entry->currentState) {
            return false;
        }

        outResource = entry->resource;
        outCurrentState = entry->currentState;
        return true;
    }
}
