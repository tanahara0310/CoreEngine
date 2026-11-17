#include "pch.h"
#include "SkinnedModelRenderer.h"


namespace CoreEngine
{
    void SkinnedModelRenderer::Initialize(ID3D12Device* device) {
        // 前方描画の VS はボーン影響（Slot1 の WEIGHT/INDEX）に対応したスキニング専用のもの
        BuildPipelines(device, {
            .forwardVS = L"Engine/Assets/Shaders/Skinning/SkinningObject3d.VS.hlsl",
            .forwardPS = L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl",
            .gBufferVS = L"Engine/Assets/Shaders/Skinning/GBufferSkinning.VS.hlsl",
            .gBufferPS = L"Engine/Assets/Shaders/Skinning/GBufferSkinning.PS.hlsl",
            .name = "SkinnedModelRenderer",
            .debugPrefix = "Skinned",
            .errorLabel = "Skinned",
            .skinned = true,
            .forwardDecls = ModelBind::kSkinnedForward,
            .gBufferDecls = ModelBind::kSkinnedGBuffer,
            });
    }
}
