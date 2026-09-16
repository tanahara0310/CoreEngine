#include "pch.h"
#include "Graphics/Render/SkyBox/SkyBoxComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"

#include <cstring>

namespace CoreEngine
{
namespace {
    struct SkyBoxVertex {
        Vector4 position;
    };
}

SkyBoxComponent::~SkyBoxComponent()
{
    for (UINT i = 0; i < kTransformBufferCount; ++i) {
        if (transformBuffers_[i] && transformData_[i]) {
            transformBuffers_[i]->Unmap(0, nullptr);
        }
    }
}

void SkyBoxComponent::Awake()
{
    GameObject* const owner = GetOwner();
    EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
    GraphicsCore* const graphics = engine ? engine->GetService<GraphicsCore>() : nullptr;
    RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
    if (!graphics || !renderManager) {
        return;
    }

    renderer_ = dynamic_cast<SkyBoxRenderer*>(renderManager->GetRenderer(RenderPassType::SkyBox));
    CreateBoxBuffers(graphics->GetDevice());
    CreateTransformBuffers(graphics->GetDevice());
}

void SkyBoxComponent::CreateBoxBuffers(ID3D12Device* device)
{
    // 原点を中心として、幅2m、高さ2mの箱を作る（x,y,zそれぞれ、-1～1ということ）
    // 内側から箱を見るので、カリングの向きが逆になる点に注意
    // 各面ごとに4頂点を定義（計24頂点）
    const SkyBoxVertex vertices[kVertexCount] = {
        // 右面: 描画インデックスは[0,1,2][2,1,3]で内側向き
        {{1.0f, 1.0f, 1.0f, 1.0f}},   // 0
        {{1.0f, 1.0f, -1.0f, 1.0f}},  // 1
        {{1.0f, -1.0f, 1.0f, 1.0f}},  // 2
        {{1.0f, -1.0f, -1.0f, 1.0f}}, // 3

        // 左面: 描画インデックスは[4,5,6][6,5,7]
        {{-1.0f, 1.0f, -1.0f, 1.0f}},  // 4
        {{-1.0f, 1.0f, 1.0f, 1.0f}},   // 5
        {{-1.0f, -1.0f, -1.0f, 1.0f}}, // 6
        {{-1.0f, -1.0f, 1.0f, 1.0f}},  // 7

        // 前面: 描画インデックスは[8,9,10][10,9,11]
        {{-1.0f, 1.0f, 1.0f, 1.0f}},  // 8
        {{1.0f, 1.0f, 1.0f, 1.0f}},   // 9
        {{-1.0f, -1.0f, 1.0f, 1.0f}}, // 10
        {{1.0f, -1.0f, 1.0f, 1.0f}},  // 11

        // 後面: 描画インデックスは[12,13,14][14,13,15]
        {{1.0f, 1.0f, -1.0f, 1.0f}},   // 12
        {{-1.0f, 1.0f, -1.0f, 1.0f}},  // 13
        {{1.0f, -1.0f, -1.0f, 1.0f}},  // 14
        {{-1.0f, -1.0f, -1.0f, 1.0f}}, // 15

        // 上面: 描画インデックスは[16,17,18][18,17,19]
        {{-1.0f, 1.0f, -1.0f, 1.0f}}, // 16
        {{1.0f, 1.0f, -1.0f, 1.0f}},  // 17
        {{-1.0f, 1.0f, 1.0f, 1.0f}},  // 18
        {{1.0f, 1.0f, 1.0f, 1.0f}},   // 19

        // 下面: 描画インデックスは[20,21,22][22,21,23]
        {{-1.0f, -1.0f, 1.0f, 1.0f}},  // 20
        {{1.0f, -1.0f, 1.0f, 1.0f}},   // 21
        {{-1.0f, -1.0f, -1.0f, 1.0f}}, // 22
        {{1.0f, -1.0f, -1.0f, 1.0f}},  // 23
    };

    // インデックスデータ（36インデックス = 12三角形 = 6面 × 2三角形）
    const uint32_t indices[kIndexCount] = {
        0, 1, 2, 2, 1, 3,         // 右面
        4, 5, 6, 6, 5, 7,         // 左面
        8, 9, 10, 10, 9, 11,      // 前面
        12, 13, 14, 14, 13, 15,   // 後面
        16, 17, 18, 18, 17, 19,   // 上面
        20, 21, 22, 22, 21, 23,   // 下面
    };

    vertexBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(vertices));
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(SkyBoxVertex);

    SkyBoxVertex* vertexData = nullptr;
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices, sizeof(vertices));
    vertexBuffer_->Unmap(0, nullptr);

    indexBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(indices));
    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(indices);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* indexData = nullptr;
    indexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::memcpy(indexData, indices, sizeof(indices));
    indexBuffer_->Unmap(0, nullptr);
}

void SkyBoxComponent::CreateTransformBuffers(ID3D12Device* device)
{
    // SceneView と GameView が同一フレーム内で空を描くので、
    // 1本のCBVを上書きすると記録済みコマンドのWVPまで変わってしまう。
    // 複数スロットを巡回して、各Drawが独立したCBVを参照するようにする。
    for (UINT i = 0; i < kTransformBufferCount; ++i) {
        transformBuffers_[i] = ResourceFactory::CreateBufferResource(device, sizeof(TransformationMatrix));
        transformBuffers_[i]->Map(0, nullptr, reinterpret_cast<void**>(&transformData_[i]));
    }
}

void SkyBoxComponent::Update()
{
    // 空の回転と強さを、IBL の回転と強さとしてシーン全体へ渡す
    const GameObject* const owner = GetOwner();
    EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
    if (RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr) {
        renderManager->SetIBLRotation(rotation_);
        renderManager->SetEnvironmentIntensity(environmentIntensity_);
    }
}

void SkyBoxComponent::Render(const DrawViewInfo& view)
{
    const GameObject* const owner = GetOwner();
    const Camera* const camera = view.GetCamera();
    ID3D12GraphicsCommandList* const commandList = view.cmdList;
    if (!owner || !owner->IsActive() || !camera || !commandList || !renderer_ || !vertexBuffer_) {
        return;
    }

    // RootSignature / PSO は BeginPass が設定済みなので、ここでは定数バッファと LUT を差すだけ
    if (!renderer_->IsPipelineReady()) {
        return;
    }
    EngineSystem* const engine = owner->GetEngineSystem();
    RenderDomainContext* const domainContext = engine ? engine->GetRenderDomainContext() : nullptr;
    AtmosphereManager* const atmosphereManager = domainContext ? domainContext->GetAtmosphereManager() : nullptr;
    if (!atmosphereManager || !atmosphereManager->IsConstantBufferReady()) {
        return;
    }

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // 平行移動を含む View 行列をそのまま使うと、遠くを追うカメラで精度が落ちて見た目が崩れる
    const Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(
        Vector3{ 1.0f, 1.0f, 1.0f }, rotation_, Vector3{ 0.0f, 0.0f, 0.0f });

    Matrix4x4 viewNoTranslation = camera->GetViewMatrix();
    viewNoTranslation.m[3][0] = 0.0f;
    viewNoTranslation.m[3][1] = 0.0f;
    viewNoTranslation.m[3][2] = 0.0f;

    transformBufferIndex_ = (transformBufferIndex_ + 1) % kTransformBufferCount;
    transformData_[transformBufferIndex_]->WVP = worldMatrix * viewNoTranslation * camera->GetProjectionMatrix();

    commandList->SetGraphicsRootConstantBufferView(
        renderer_->GetRootParamIndex("gTransformationMatrix"),
        transformBuffers_[transformBufferIndex_]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        renderer_->GetRootParamIndex("gAtmosphere"),
        atmosphereManager->GetConstantBufferGPUAddress());

    // LUT 群（未生成の初回フレームは黒がサンプルされるだけなので許容）
    // 描画は Sky-View LUT の1サンプルで完結する。Transmittance は太陽ディスク描画で使用。
    const int skyViewParamIndex = renderer_->GetRootParamIndex("gSkyViewLUT");
    if (skyViewParamIndex >= 0) {
        commandList->SetGraphicsRootDescriptorTable(
            static_cast<UINT>(skyViewParamIndex), atmosphereManager->GetSkyViewLUTSRVHandle());
    }
    const int transmittanceParamIndex = renderer_->GetRootParamIndex("gTransmittanceLUT");
    if (transmittanceParamIndex >= 0) {
        commandList->SetGraphicsRootDescriptorTable(
            static_cast<UINT>(transmittanceParamIndex), atmosphereManager->GetTransmittanceLUTSRVHandle());
    }

    commandList->DrawIndexedInstanced(kIndexCount, 1, 0, 0, 0);
}
}
