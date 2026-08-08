#include "pch.h"
#include "SkyBoxObject.h"
#include "Camera/Camera.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Math/MathCore.h"
#include "Editor/ImGui/ImGuiAll.h"
#include <cassert>
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace {

    CoreEngine::SkyBoxRenderer* sSkyBoxRenderer_ = nullptr;
}

void SkyBoxObject::SetSkyBoxRenderer(SkyBoxRenderer* renderer) {
    sSkyBoxRenderer_ = renderer;
}

struct SkyBoxVertex {
    Vector4 position; // Vector3からVector4に変更
};

void SkyBoxObject::Initialize() {

    // トランスフォーム初期化
    transform_.scale     = { 1.0f, 1.0f, 1.0f };
    transform_.rotate    = { 0.0f, 0.0f, 0.0f };
    transform_.translate = { 0.0f, 0.0f, 0.0f };

    // 頂点データ生成
    CreateBoxVertices();

    // トランスフォーム用定数バッファ生成
    CreateTransformBuffer();

    // SkyBox はシーン JSON のシリアライズ対象外（回転・輝度はコードで制御）
    SetSerializeEnabled(false);
}

void SkyBoxObject::CreateBoxVertices() {
    auto engine = GetEngineSystem();
    auto dxCommon = engine->GetService<DirectXCommon>();
    assert(dxCommon != nullptr);

    // 原点を中心として、幅2m、高さ2mの箱を作る（x,y,zそれぞれ、-1～1ということ）
    // 内側から箱を見るので、カリングの向きが逆になる点に注意
    // 各面ごとに4頂点を定義（計24頂点）

    // 頂点データ（24頂点 = 6面 × 4頂点）
    SkyBoxVertex vertices[kVertexCount] = {
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
    uint32_t indices[kIndexCount] = {
        // 右面 [0,1,2][2,1,3]
        0, 1, 2,
        2, 1, 3,

        // 左面 [4,5,6][6,5,7]
        4, 5, 6,
        6, 5, 7,

        // 前面 [8,9,10][10,9,11]
        8, 9, 10,
        10, 9, 11,

        // 後面 [12,13,14][14,13,15]
        12, 13, 14,
        14, 13, 15,

        // 上面 [16,17,18][18,17,19]
        16, 17, 18,
        18, 17, 19,

        // 下面 [20,21,22][22,21,23]
        20, 21, 22,
        22, 21, 23,
    };

    // 頂点バッファの作成
    vertexBuffer_ = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), sizeof(vertices));

    // 頂点バッファビューの作成
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(SkyBoxVertex);

    // 頂点データをマップ
    SkyBoxVertex* vertexData = nullptr;
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, vertices, sizeof(vertices));
    vertexBuffer_->Unmap(0, nullptr);

    // インデックスバッファの作成
    indexBuffer_ = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), sizeof(indices));

    // インデックスバッファビューの作成
    indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(indices);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    // インデックスデータをマップ
    uint32_t* indexData = nullptr;
    indexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::memcpy(indexData, indices, sizeof(indices));
    indexBuffer_->Unmap(0, nullptr);
}

void SkyBoxObject::Update() {
    if (!IsActive()) return;
    // スカイボックスのXYZ回転をIBL回転としてシーン全体へ伝播
    auto engine = GetEngineSystem();
    if (auto* renderManager = engine->GetService<RenderManager>()) {
        renderManager->SetIBLRotation(transform_.rotate);
        // SkyBox 輝度スケールを IBL に伝播（環境マップの映り込みも連動）
        renderManager->SetEnvironmentIntensity(environmentIntensity_);
    }
}

void SkyBoxObject::CreateTransformBuffer() {
    auto engine = GetEngineSystem();
    auto* dxCommon = engine->GetService<DirectXCommon>();
    assert(dxCommon != nullptr);

    // SceneView と GameView が同一フレーム内で SkyBox を描画するため、
    // 1本のCBVを上書きすると記録済みコマンドのWVPまで変わってしまう。
    // 複数スロットを巡回して、各Drawが独立したCBVを参照するようにする。
    for (UINT i = 0; i < kTransformBufferCount; ++i) {
        transformBuffers_[i] = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), sizeof(TransformationMatrix));
        transformBuffers_[i]->Map(0, nullptr, reinterpret_cast<void**>(&transformData_[i]));
    }
}

void SkyBoxObject::Draw(const CoreEngine::Camera* camera) {
    if (!camera) return;
    auto engine = GetEngineSystem();
    auto* dxCommon = engine->GetService<DirectXCommon>();
    auto* renderManager = engine->GetService<RenderManager>();
    assert(dxCommon != nullptr);
    assert(renderManager != nullptr);

    auto* commandList = dxCommon->GetCommandList();

    // RenderManagerからSkyBoxRendererを取得
    auto* skyBoxRenderer = renderManager->GetRenderer(RenderPassType::SkyBox);
    if (!skyBoxRenderer) return;

    // 頂点バッファの設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // SkyBoxは平行移動の影響を受けないようにする。
    // カメラ平行移動を含むView行列をそのまま使うと追従カメラ時に精度劣化で見た目が崩れる場合がある
    Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(
        transform_.scale,
        transform_.rotate,
        transform_.translate
    );

    Matrix4x4 viewNoTranslation = camera->GetViewMatrix();
    viewNoTranslation.m[3][0] = 0.0f;
    viewNoTranslation.m[3][1] = 0.0f;
    viewNoTranslation.m[3][2] = 0.0f;

    Matrix4x4 viewProjectionMatrix = MathCore::Matrix::Multiply(
        viewNoTranslation,
        camera->GetProjectionMatrix()
    );

    transformBufferIndex_ = (transformBufferIndex_ + 1) % kTransformBufferCount;
    transformData_[transformBufferIndex_]->WVP = MathCore::Matrix::Multiply(worldMatrix, viewProjectionMatrix);

    assert(sSkyBoxRenderer_ != nullptr);

    // 空は常に大気散乱で描く。RootSignature / PSO は BeginPass が設定済みなので、
    // ここでは定数バッファと LUT のバインドだけを行う。
    if (!sSkyBoxRenderer_->IsPipelineReady()) return;

    auto* domainContext = engine->GetRenderDomainContext();
    auto* atmosphereManager = domainContext ? domainContext->GetAtmosphereManager() : nullptr;
    if (!atmosphereManager || !atmosphereManager->IsConstantBufferReady()) return;

    commandList->SetGraphicsRootConstantBufferView(
        sSkyBoxRenderer_->GetRootParamIndex("gTransformationMatrix"),
        transformBuffers_[transformBufferIndex_]->GetGPUVirtualAddress()
    );
    commandList->SetGraphicsRootConstantBufferView(
        sSkyBoxRenderer_->GetRootParamIndex("gAtmosphere"),
        atmosphereManager->GetConstantBufferGPUAddress()
    );

    // LUT 群（未生成の初回フレームは黒がサンプルされるだけなので許容）
    // 描画は Sky-View LUT の1サンプルで完結する。Transmittance は太陽ディスク描画で使用。
    const int skyViewParamIndex = sSkyBoxRenderer_->GetRootParamIndex("gSkyViewLUT");
    if (skyViewParamIndex >= 0) {
        commandList->SetGraphicsRootDescriptorTable(
            static_cast<UINT>(skyViewParamIndex),
            atmosphereManager->GetSkyViewLUTSRVHandle()
        );
    }
    const int transmittanceParamIndex = sSkyBoxRenderer_->GetRootParamIndex("gTransmittanceLUT");
    if (transmittanceParamIndex >= 0) {
        commandList->SetGraphicsRootDescriptorTable(
            static_cast<UINT>(transmittanceParamIndex),
            atmosphereManager->GetTransmittanceLUTSRVHandle()
        );
    }

    // 描画コマンド
    commandList->DrawIndexedInstanced(kIndexCount, 1, 0, 0, 0);
}

#ifdef _DEBUG
int SkyBoxObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
    if (maxTabs < 1) return 0;
    outTabs[0] = { "object_data.png", "トランスフォーム", {0.96f,0.65f,0.14f,1.0f}, {0.96f,0.65f,0.14f,0.25f} };
    return 1;
}

bool SkyBoxObject::DrawInspectorTabContent(int tabIndex) {
    switch (tabIndex) {
    case 0: return DrawTransformSection();
    default: return false;
    }
}

bool SkyBoxObject::DrawTransformSection() {
    bool changed = false;

    UI::SectionHeader("回転");
    changed |= UI::DragVec3("回転", transform_.rotate, 0.01f);

    UI::SectionHeader("スケール");
    changed |= UI::DragVec3("スケール", transform_.scale, 0.01f);

    UI::SectionHeader("環境設定");
    if (ImGui::SliderFloat("輝度スケール", &environmentIntensity_, 0.01f, 5.0f, "%.2f")) {
        changed = true;
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "環境マップ（映り込み）の明るさを調整します");

    UI::Spacing();
    if (ImGui::Button("リセット##skybox")) {
        transform_.scale  = { 1.0f, 1.0f, 1.0f };
        transform_.rotate = { 0.0f, 0.0f, 0.0f };
        changed = true;
    }

    return changed;
}

#endif

