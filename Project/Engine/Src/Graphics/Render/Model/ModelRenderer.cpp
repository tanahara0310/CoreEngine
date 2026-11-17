#include "pch.h"
#include "ModelRenderer.h"


namespace CoreEngine
{
    void ModelRenderer::Initialize(ID3D12Device* device) {
        BuildPipelines(device, {
            .forwardVS = L"Engine/Assets/Shaders/Object/Object3d.VS.hlsl",
            .forwardPS = L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl",
            .gBufferVS = L"Engine/Assets/Shaders/Object/GBuffer.VS.hlsl",
            .gBufferPS = L"Engine/Assets/Shaders/Object/GBuffer.PS.hlsl",
            .name = "ModelRenderer",
            .debugPrefix = "Model",
            .errorLabel = "",
            .skinned = false,
            .forwardDecls = ModelBind::kForward,
            .gBufferDecls = ModelBind::kGBuffer,
            });
    }
}
