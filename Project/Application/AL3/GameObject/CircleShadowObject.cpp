#include "CircleShadowObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Structs/TransformationMatrix.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Camera/ICamera.h"
#include <cassert>

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#endif

void CircleShadowObject::Initialize(float shadowSize) {
    auto* engine = GetEngineSystem();
    auto* dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
    auto* resourceFactory = engine->GetComponent<CoreEngine::ResourceFactory>();

    assert(dxCommon && resourceFactory);

    shadowSize_ = shadowSize;

    // プリミティブPlaneを作成
    plane_ = std::make_unique<CoreEngine::PrimitivePlane>();
    plane_->Initialize(dxCommon, resourceFactory, shadowSize, shadowSize);

    // マテリアルを作成
    materialManager_ = std::make_unique<CoreEngine::MaterialManager>();
    materialManager_->Initialize(dxCommon->GetDevice(), resourceFactory);
    materialManager_->SetEnableLighting(false);  // ライティング無効
    materialManager_->SetEnableDithering(false); // ディザリング無効（アルファブレンド使用）
    materialManager_->SetColor({ 0.0f, 0.0f, 0.0f, shadowAlpha_ });  // 完全な黒色

    // WVP行列用リソースを作成
    wvpResource_ = CoreEngine::ResourceFactory::CreateBufferResource(
        dxCommon->GetDevice(),
        sizeof(CoreEngine::TransformationMatrix)
    );
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));

    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());
    transform_.scale = { 1.0f, 1.0f, 1.0f };

    // 丸影テクスチャを読み込み
    auto& textureManager = CoreEngine::TextureManager::GetInstance();
    texture_ = textureManager.Load("Assets/Texture/MyCircle.png");

    SetActive(true);
}

void CircleShadowObject::Update() {
    if (!IsActive()) return;

    // トランスフォームの更新
    transform_.TransferMatrix();
}

void CircleShadowObject::Draw(const CoreEngine::ICamera* camera) {
if (!IsActive() || !plane_ || !camera) return;

auto* engine = GetEngineSystem();
auto* dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
auto* cmdList = dxCommon->GetCommandList();

// WVP行列を更新
UpdateTransformationMatrix(camera);

// 頂点バッファを設定
cmdList->IASetVertexBuffers(0, 1, &plane_->GetVertexBufferView());

// インデックスバッファを設定
cmdList->IASetIndexBuffer(&plane_->GetIndexBufferView());

// マテリアルを設定（Root Parameter 0）
cmdList->SetGraphicsRootConstantBufferView(
    CoreEngine::ModelRendererRootParam::kMaterial,
    materialManager_->GetGPUVirtualAddress()
    );

    // WVP行列を設定（Root Parameter 1）
    cmdList->SetGraphicsRootConstantBufferView(
        CoreEngine::ModelRendererRootParam::kWVP,
        wvpResource_->GetGPUVirtualAddress()
    );

    // テクスチャを設定（Root Parameter 2）
    cmdList->SetGraphicsRootDescriptorTable(
        CoreEngine::ModelRendererRootParam::kTexture,
        texture_.gpuHandle
    );

    // 描画
    cmdList->DrawIndexedInstanced(plane_->GetIndexCount(), 1, 0, 0, 0);
}

void CircleShadowObject::SetTargetPosition(const CoreEngine::Vector3& targetPosition) {
    // 影はターゲットのXZ位置に追従し、Y座標は設定された高さ
    transform_.translate.x = targetPosition.x;
    transform_.translate.y = shadowY_;
    transform_.translate.z = targetPosition.z;
}

void CircleShadowObject::SetShadowSize(float size) {
    shadowSize_ = size;
    if (plane_) {
        plane_->Resize(size, size);
    }
}

void CircleShadowObject::SetShadowAlpha(float alpha) {
    shadowAlpha_ = alpha;
    if (materialManager_) {
        // 黒色を維持し、アルファ値のみ変更
        materialManager_->SetColor({ 0.0f, 0.0f, 0.0f, shadowAlpha_ });
    }
}

void CircleShadowObject::UpdateTransformationMatrix(const CoreEngine::ICamera* camera) {
    if (!wvpData_) return;

    // ワールド行列を計算
    CoreEngine::Matrix4x4 worldMatrix = transform_.GetWorldMatrix();
    CoreEngine::Matrix4x4 viewMatrix = camera->GetViewMatrix();
    CoreEngine::Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
    CoreEngine::Matrix4x4 wvpMatrix = CoreEngine::MathCore::Matrix::Multiply(
        worldMatrix,
        CoreEngine::MathCore::Matrix::Multiply(viewMatrix, projectionMatrix)
    );

    // GPUメモリに書き込み
    wvpData_->WVP = wvpMatrix;
    wvpData_->world = worldMatrix;
    wvpData_->worldInverseTranspose = CoreEngine::MathCore::Matrix::Transpose(
        CoreEngine::MathCore::Matrix::Inverse(worldMatrix)
    );
}

#ifdef _DEBUG
bool CircleShadowObject::DrawImGuiExtended() {
    bool changed = false;

    ImGui::SeparatorText("Shadow Parameters");

    // 影のサイズ
    float size = shadowSize_;
    if (ImGui::DragFloat("Shadow Size", &size, 0.1f, 0.5f, 20.0f, "%.1f")) {
        SetShadowSize(size);
        changed = true;
    }

    // 影の濃さ（アルファ値）
    float alpha = shadowAlpha_;
    if (ImGui::SliderFloat("Shadow Alpha", &alpha, 0.0f, 1.0f, "%.2f")) {
        SetShadowAlpha(alpha);
        changed = true;
    }

    // 影のY座標
    if (ImGui::DragFloat("Shadow Y", &shadowY_, 0.1f, -100.0f, 100.0f, "%.1f")) {
        changed = true;
    }

    ImGui::SeparatorText("Position");

    // 現在の位置を表示
    ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f",
        transform_.translate.x,
        transform_.translate.y,
        transform_.translate.z);

    // 位置を直接編集（デバッグ用）
    CoreEngine::Vector3 pos = transform_.translate;
    if (ImGui::DragFloat3("Manual Position", &pos.x, 0.1f)) {
        transform_.translate = pos;
        changed = true;
    }

    ImGui::SeparatorText("Material");

    // マテリアルの色（デバッグ用）
    if (materialManager_) {
        CoreEngine::Vector4 color = materialManager_->GetColor();
        if (ImGui::ColorEdit4("Material Color", &color.x)) {
            materialManager_->SetColor(color);
            shadowAlpha_ = color.w;
            changed = true;
        }
    }

    return changed;
}
#endif
