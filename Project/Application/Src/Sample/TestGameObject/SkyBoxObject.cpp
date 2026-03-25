#include "SkyBoxObject.h"
#include "Camera/ICamera.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Material/SkyBoxMaterialInstance.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Math/MathCore.h"
#include "externals/imgui/imgui.h"
#include <cassert>
#include "EngineSystem/EngineSystem.h"

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

    // マテリアル初期化
    auto engine = GetEngineSystem();
    auto* dxCommon = engine->GetComponent<DirectXCommon>();
    assert(dxCommon != nullptr);
    material_ = std::make_unique<SkyBoxMaterialInstance>();
    material_->Initialize(dxCommon->GetDevice());

    // トランスフォーム用定数バッファ生成
    CreateTransformBuffer();

    // SkyBox はシーン JSON のシリアライズ対象外（テクスチャ・回転はコードで制御）
    SetSerializeEnabled(false);
}

void SkyBoxObject::CreateBoxVertices() {
    auto engine = GetEngineSystem();
    auto dxCommon = engine->GetComponent<DirectXCommon>();
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
    if (auto* renderManager = engine->GetComponent<RenderManager>()) {
        renderManager->SetIBLRotation(transform_.rotate);
    }
}

void SkyBoxObject::CreateTransformBuffer() {
    auto engine = GetEngineSystem();
    auto* dxCommon = engine->GetComponent<DirectXCommon>();
    assert(dxCommon != nullptr);

    // トランスフォーム用定数バッファの作成
    transformBuffer_ = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), sizeof(TransformationMatrix));

    // トランスフォームデータをマップ
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
}

void SkyBoxObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera) return;
    auto engine = GetEngineSystem();
    auto* dxCommon = engine->GetComponent<DirectXCommon>();
    auto* renderManager = engine->GetComponent<RenderManager>();
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

    transformData_->WVP = MathCore::Matrix::Multiply(worldMatrix, viewProjectionMatrix);

    assert(sSkyBoxRenderer_ != nullptr);

    // トランスフォーム行列CBV
    commandList->SetGraphicsRootConstantBufferView(
        sSkyBoxRenderer_->GetRootParamIndex("gTransformationMatrix"),
        transformBuffer_->GetGPUVirtualAddress()
    );

    // マテリアルCBV
    commandList->SetGraphicsRootConstantBufferView(
        sSkyBoxRenderer_->GetRootParamIndex("gMaterial"),
        material_->GetGPUVirtualAddress()
    );

    // テクスチャの設定
    commandList->SetGraphicsRootDescriptorTable(
        sSkyBoxRenderer_->GetRootParamIndex("gTexture"),
        texture_.gpuHandle
    );

    // 描画コマンド
    commandList->DrawIndexedInstanced(kIndexCount, 1, 0, 0, 0);
}

#ifdef _DEBUG
bool SkyBoxObject::DrawImGuiExtended() {
    bool changed = false;

    if (ImGui::TreeNode("Transform")) {
        changed |= ImGui::DragFloat3("Rotation", &transform_.rotate.x, 0.01f);
        changed |= ImGui::DragFloat3("Scale",    &transform_.scale.x,  0.01f);
        ImGui::TreePop();
    }

    if (ImGui::Button("回転・スケールをリセット")) {
        transform_.scale  = { 1.0f, 1.0f, 1.0f };
        transform_.rotate = { 0.0f, 0.0f, 0.0f };
        changed = true;
    }

    return changed;
}
#endif

