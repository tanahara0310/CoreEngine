#include "pch.h"
#include "SpriteObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Math/MathCore.h"
#include <cmath>
#include <cstdio>
#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{

    using namespace CoreEngine::MathCore;

    void SpriteObject::Initialize(const std::string& textureFilePath, const std::string& name) {
        auto engine = GetEngineSystem();

        // オブジェクト名を設定
        if (!name.empty()) {
            name_ = name;
        }

        // SpriteRendererを取得
        auto* renderManager = engine->GetService<RenderManager>();
        spriteRenderer_ = dynamic_cast<SpriteRenderer*>(renderManager->GetRenderer(RenderPassType::Sprite));

        // テクスチャの読み込み
        textureHandle_ = TextureManager::GetInstance().Load(textureFilePath);

        // テクスチャサイズを自動設定
        SetSizeFromTexture(textureFilePath);

        // 頂点バッファを作成
        CreateVertexBuffer();

        // マテリアルインスタンスを生成
        material_ = std::make_unique<SpriteMaterialInstance>();
        material_->Initialize(spriteRenderer_->GetDirectXCommon()->GetDevice());

        // デフォルト値を設定
        Reset();

        // アクティブ状態
        isActive_ = true;

        // 初期化時は頂点データを更新
        vertexDataDirty_ = false; // CreateVertexBuffer内でUpdateVertexDataが呼ばれるため
    }

    void SpriteObject::SetSizeFromTexture(const std::string& textureFilePath) {
        auto& textureManager = TextureManager::GetInstance();
        DirectX::TexMetadata metadata = textureManager.GetMetadata(textureFilePath);

        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);
    }

    void SpriteObject::CreateVertexBuffer() {
        if (!spriteRenderer_) return;

        DirectXCommon* dxCommon = spriteRenderer_->GetDirectXCommon();
        ResourceFactory* resourceFactory = spriteRenderer_->GetResourceFactory();

        if (!dxCommon || !resourceFactory) return;

        // 頂点バッファの生成（4頂点のクワッド）
        vertexResource_ = resourceFactory->CreateBufferResource(
            dxCommon->GetDevice(),
            sizeof(VertexData) * 4);

        vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        // インデックスバッファの生成
        indexResource_ = resourceFactory->CreateBufferResource(
            dxCommon->GetDevice(),
            sizeof(uint32_t) * 6);

        uint32_t* indexData = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));

        indexData[0] = 0; indexData[1] = 1; indexData[2] = 2;
        indexData[3] = 1; indexData[4] = 3; indexData[5] = 2;

        indexResource_->Unmap(0, nullptr);

        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

        UpdateVertexData();
    }

    void SpriteObject::UpdateVertexData() {
        if (!vertexResource_) return;

        VertexData* vertexData = nullptr;
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

        // アンカーポイントを考慮したローカル座標
        float left = -anchorPoint_.x;
        float right = 1.0f - anchorPoint_.x;
        float top = anchorPoint_.y;
        float bottom = anchorPoint_.y - 1.0f;

        // フリップを考慮した UV 範囲を計算
        float uMin = flipX_ ? uvMax_.x : uvMin_.x;
        float uMax = flipX_ ? uvMin_.x : uvMax_.x;
        float vMin = flipY_ ? uvMax_.y : uvMin_.y;
        float vMax = flipY_ ? uvMin_.y : uvMax_.y;

        // 左下
        vertexData[0].position = { left,  bottom, 0.0f, 1.0f };
        vertexData[0].texcoord = { uMin, vMax };
        vertexData[0].normal = { 0.0f, 0.0f, -1.0f };

        // 左上
        vertexData[1].position = { left,  top,    0.0f, 1.0f };
        vertexData[1].texcoord = { uMin, vMin };
        vertexData[1].normal = { 0.0f, 0.0f, -1.0f };

        // 右下
        vertexData[2].position = { right, bottom, 0.0f, 1.0f };
        vertexData[2].texcoord = { uMax, vMax };
        vertexData[2].normal = { 0.0f, 0.0f, -1.0f };

        // 右上
        vertexData[3].position = { right, top,    0.0f, 1.0f };
        vertexData[3].texcoord = { uMax, vMin };
        vertexData[3].normal = { 0.0f, 0.0f, -1.0f };

        vertexResource_->Unmap(0, nullptr);
    }

    void SpriteObject::Update() {
        if (!isActive_) return;

        // アニメーターの更新
        if (animator_ && animator_->IsPlaying()) {
            float deltaTime = 0.0f;
            if (auto* fr = GetEngineSystem()->GetService<FrameRateController>()) {
                deltaTime = fr->GetDeltaTime();
            }
            animator_->Update(deltaTime, this);
        }

        // 頂点データが変更されている場合のみ更新
        if (vertexDataDirty_) {
            UpdateVertexData();
            vertexDataDirty_ = false;
        }
    }

    SpriteAnimator& SpriteObject::GetAnimator() {
        if (!animator_) {
            animator_ = std::make_unique<SpriteAnimator>();
        }
        return *animator_;
    }

    void SpriteObject::Draw2D(const Camera* camera, ID3D12GraphicsCommandList* commandList) {
        if (!spriteRenderer_) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: SpriteRenderer is null in SpriteObject::Draw2D!\n");
#endif
            return;
        }

        // 積み先はキュー実行側が DrawViewInfo で渡す。ここで自分から取りに行くと、
        // 呼び出し元が「どのコマンドリストへ積むか」を制御できなくなる。
        if (!commandList) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: commandList is null in SpriteObject::Draw2D!\n");
#endif
            return;
        }

        size_t bufferIndex = spriteRenderer_->GetAvailableConstantBuffer();

        // 実際の描画サイズを計算（テクスチャサイズ × スケール）
        Vector3 actualScale = {
            textureSize_.x * transform_.scale.x,
            textureSize_.y * transform_.scale.y,
            transform_.scale.z
        };

        // 変換行列設定（必ず最新の値で上書き）
        auto& transformData = spriteRenderer_->GetTransformDataPool()[bufferIndex];
        Matrix4x4 worldMatrix = Matrix::MakeAffine(actualScale, transform_.rotate, transform_.translate);

        // カメラを使用してWVP行列を計算
        transformData->WVP = spriteRenderer_->CalculateWVPMatrix(transform_.translate, actualScale, transform_.rotate, camera);
        transformData->world = worldMatrix;

        // 定数バッファ設定（シェーダーリフレクションから取得したインデックスを使用）
        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("gMaterial"),
            material_->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("TransformationMatrix"),
            spriteRenderer_->GetTransformResource(bufferIndex)->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(
            spriteRenderer_->GetRootParamIndex("gTexture"),
            textureHandle_.gpuHandle);

        // 頂点バッファ・インデックスバッファを設定
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commandList->IASetIndexBuffer(&indexBufferView_);

        // 描画
        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    void SpriteObject::Reset() {
        transform_.scale = { 1.0f, 1.0f, 1.0f };
        transform_.rotate = { 0.0f, 0.0f, 0.0f };
        transform_.translate = { 0.0f, 0.0f, 0.0f };
        material_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        material_->SetUVTransform(Matrix::Identity());
        uvTransform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
        anchorPoint_ = { 0.5f, 0.5f };
        uvMin_ = { 0.0f, 0.0f };
        uvMax_ = { 1.0f, 1.0f };
        flipX_ = false;
        flipY_ = false;

        vertexDataDirty_ = true;
    }

    void SpriteObject::SetTexture(const std::string& textureFilePath) {
        textureHandle_ = TextureManager::GetInstance().Load(textureFilePath);
        SetSizeFromTexture(textureFilePath);
    }

    void SpriteObject::SetAnchor(const Vector2& anchor) {
        if (anchorPoint_.x != anchor.x || anchorPoint_.y != anchor.y) {
            anchorPoint_ = anchor;
            vertexDataDirty_ = true;
        }
    }

    void SpriteObject::SetTextureRect(float texLeft, float texTop, float texWidth, float texHeight,
        const std::string& textureFilePath) {
        auto& textureManager = TextureManager::GetInstance();
        DirectX::TexMetadata metadata = textureManager.GetMetadata(textureFilePath);

        float textureWidth = static_cast<float>(metadata.width);
        float textureHeight = static_cast<float>(metadata.height);

        uvMin_.x = texLeft / textureWidth;
        uvMin_.y = texTop / textureHeight;
        uvMax_.x = (texLeft + texWidth) / textureWidth;
        uvMax_.y = (texTop + texHeight) / textureHeight;

        vertexDataDirty_ = true;
    }

    void SpriteObject::SetUVRect(float uvLeft, float uvTop, float uvRight, float uvBottom) {
        uvMin_.x = uvLeft;
        uvMin_.y = uvTop;
        uvMax_.x = uvRight;
        uvMax_.y = uvBottom;

        vertexDataDirty_ = true;
    }

    void SpriteObject::SetUVOffset(float offsetX, float offsetY) {
        uvTransform_.translate = { offsetX, offsetY, 0.0f };
        UpdateUVTransformMatrix(uvTransform_);
    }

    void SpriteObject::SetUVScale(float scaleX, float scaleY) {
        uvTransform_.scale = { scaleX, scaleY, 1.0f };
        UpdateUVTransformMatrix(uvTransform_);
    }

    void SpriteObject::SetUVRotation(float rotation) {
        uvTransform_.rotate.z = rotation;
        UpdateUVTransformMatrix(uvTransform_);
    }

    void SpriteObject::ResetUVTransform() {
        uvTransform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
        material_->SetUVTransform(Matrix::Identity());
    }

    void SpriteObject::SetSortingLayer(int layer) {
        sortingLayer_ = layer;
        SetRenderOrder(sortingLayer_ * 1000 + orderInLayer_);
    }

    void SpriteObject::SetOrderInLayer(int order) {
        orderInLayer_ = order;
        SetRenderOrder(sortingLayer_ * 1000 + orderInLayer_);
    }

    void SpriteObject::SetFlipX(bool flip) {
        if (flipX_ != flip) {
            flipX_ = flip;
            vertexDataDirty_ = true;
        }
    }

    void SpriteObject::SetFlipY(bool flip) {
        if (flipY_ != flip) {
            flipY_ = flip;
            vertexDataDirty_ = true;
        }
    }

    void SpriteObject::UpdateUVTransformMatrix(const EulerTransform& uvTransform) {
        Matrix4x4 scaleMatrix = Matrix::Scale(uvTransform.scale);
        Matrix4x4 rotateMatrix = Matrix::RotationZ(uvTransform.rotate.z);
        Matrix4x4 translateMatrix = Matrix::Translation(uvTransform.translate);
        material_->SetUVTransform(scaleMatrix * rotateMatrix * translateMatrix);
    }

    void SpriteObject::ChangeAnchorKeepingPosition(const Vector2& newAnchor) {
        if (anchorPoint_.x != newAnchor.x || anchorPoint_.y != newAnchor.y) {
            anchorPoint_ = newAnchor;
            vertexDataDirty_ = true;
        }
    }

#ifdef USE_IMGUI
    void SpriteObject::OnImGuiActiveChanged(bool prevActive) {
        if (onEditCommitted_) {
            onEditCommitted_(this,
                transform_.translate, transform_.rotate, transform_.scale, prevActive);
        }
    }

    int SpriteObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
        if (maxTabs < 3) return 0;
        outTabs[0] = { "object_data.png", "トランスフォーム", {0.96f,0.65f,0.14f,1.0f}, {0.96f,0.65f,0.14f,0.25f} };
        outTabs[1] = { "material.png",    "マテリアル",   {0.90f,0.30f,0.40f,1.0f}, {0.90f,0.30f,0.40f,0.25f} };
        outTabs[2] = { "imagePlane.png",  "スプライト",   {0.60f,0.40f,0.80f,1.0f}, {0.60f,0.40f,0.80f,0.25f} };
        return 3;
    }

    bool SpriteObject::DrawInspectorTabContent(int tabIndex) {
        bool changed = false;
        // タブ番号は Inspector 側の並びと 1 対 1（0=トランスフォーム / 1=マテリアル / 2=スプライト）

        switch (tabIndex) {
        case 0: { // ── トランスフォーム ───────────────
            // ドラッグ開始時の値を控え、確定時に 1 回だけ Undo へ積む
            // （ドラッグ中の毎フレーム変化を積むと Undo が 1 ピクセルずつ戻る）
            auto snapAndCommit = [&](auto editFn) {
                editFn();
                if (ImGui::IsItemActivated()) {
                    imguiSnapTranslate_ = transform_.translate;
                    imguiSnapRotate_ = transform_.rotate;
                    imguiSnapScale_ = transform_.scale;
                    imguiSnapActive_ = isActive_;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                    onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
                }
                };

            UI::SectionHeader("位置");
            snapAndCommit([&] { changed |= UI::DragVec3("位置", transform_.translate, 0.5f); });

            UI::SectionHeader("回転");
            snapAndCommit([&] { changed |= UI::DragVec3("回転", transform_.rotate, 0.01f); });

            UI::SectionHeader("スケール");
            snapAndCommit([&] { changed |= UI::DragVec3("スケール", transform_.scale, 0.01f, 0.0f, 100.0f); });

            UI::Spacing();
            ImGui::Text("テクスチャ: %.0f x %.0f px", textureSize_.x, textureSize_.y);
            Vector2 actualSize = GetActualSize();
            ImGui::Text("描画サイズ: %.0f x %.0f px", actualSize.x, actualSize.y);
            break;
        }
        case 1: { // ── マテリアル ───────────────
            UI::SectionHeader("基本設定");

            Vector4 color = material_->GetColor();
            if (UI::ColorEdit("カラー", color)) {
                material_->SetColor(color);
                changed = true;
            }

            UI::SectionHeader("UV 変換");

            bool uvChanged = false;
            uvChanged |= ImGui::DragFloat2("オフセット##UV", &uvTransform_.translate.x, 0.01f);
            uvChanged |= ImGui::DragFloat2("スケール##UV", &uvTransform_.scale.x, 0.01f, 0.01f, 10.0f);
            uvChanged |= UI::SliderFloat("回転##UV", uvTransform_.rotate.z, -MathCore::Constants::kPi, MathCore::Constants::kPi);

            if (uvChanged) {
                UpdateUVTransformMatrix(uvTransform_);
                changed = true;
            }

            if (ImGui::Button("UV リセット")) {
                uvTransform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
                material_->SetUVTransform(Matrix::Identity());
                changed = true;
            }
            break;
        }
        case 2: { // ── スプライト ───────────────
            UI::SectionHeader("ブレンドモード");
            {
                const char* blendModes[] = { "なし", "通常", "加算", "減算", "乗算", "スクリーン" };
                int blendModeInt = static_cast<int>(blendMode_);
                if (ImGui::Combo("##blendMode", &blendModeInt, blendModes, 6)) {
                    blendMode_ = static_cast<BlendMode>(blendModeInt);
                    changed = true;
                }
            }

            UI::SectionHeader("描画順序");
            {
                int layerTemp = sortingLayer_;
                if (ImGui::DragInt("Sorting Layer##sort", &layerTemp, 1.0f, -100, 100)) {
                    SetSortingLayer(layerTemp);
                    changed = true;
                }
                int orderTemp = orderInLayer_;
                if (ImGui::DragInt("Order In Layer##sort", &orderTemp, 1.0f, -9999, 9999)) {
                    SetOrderInLayer(orderTemp);
                    changed = true;
                }
            }

            UI::SectionHeader("フリップ");
            {
                bool fx = flipX_;
                bool fy = flipY_;
                if (ImGui::Checkbox("Flip X##flip", &fx)) { SetFlipX(fx); changed = true; }
                ImGui::SameLine();
                if (ImGui::Checkbox("Flip Y##flip", &fy)) { SetFlipY(fy); changed = true; }
            }

            UI::SectionHeader("アンカーポイント");
            Vector2 anchorTemp = anchorPoint_;
            if (UI::DragVec2("##anchor", anchorTemp, 0.01f, 0.0f, 1.0f)) {
                ChangeAnchorKeepingPosition(anchorTemp);
                changed = true;
            }
            if (ImGui::Button("TL##anchor")) { ChangeAnchorKeepingPosition({ 0.0f, 0.0f }); changed = true; } UI::SameLine();
            if (ImGui::Button("TC##anchor")) { ChangeAnchorKeepingPosition({ 0.5f, 0.0f }); changed = true; } UI::SameLine();
            if (ImGui::Button("TR##anchor")) { ChangeAnchorKeepingPosition({ 1.0f, 0.0f }); changed = true; } UI::SameLine();
            if (ImGui::Button("C##anchor")) { ChangeAnchorKeepingPosition({ 0.5f, 0.5f }); changed = true; } UI::SameLine();
            if (ImGui::Button("BL##anchor")) { ChangeAnchorKeepingPosition({ 0.0f, 1.0f }); changed = true; } UI::SameLine();
            if (ImGui::Button("BR##anchor")) { ChangeAnchorKeepingPosition({ 1.0f, 1.0f }); changed = true; }

            UI::Spacing();
            if (ImGui::Button("リセット##sprite")) {
                Reset();
                changed = true;
            }
            break;
        }
        default: break;
        }

        return changed;
    }
#endif // USE_IMGUI

    json SpriteObject::OnSerialize() const {
        json j;
        j["active"] = IsActive();
        j["transform"]["translate"] = JsonManager::Vector3ToJson(transform_.translate);
        j["transform"]["rotate"] = JsonManager::Vector3ToJson(transform_.rotate);
        j["transform"]["scale"] = JsonManager::Vector3ToJson(transform_.scale);
        j["flipX"] = flipX_;
        j["flipY"] = flipY_;
        return j;
    }

    void SpriteObject::OnDeserialize(const json& j) {
        if (j.contains("active")) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("transform")) {
            const json& t = j["transform"];
            transform_.translate = JsonManager::SafeGetVector3(t, "translate", transform_.translate);
            transform_.rotate = JsonManager::SafeGetVector3(t, "rotate", transform_.rotate);
            transform_.scale = JsonManager::SafeGetVector3(t, "scale", transform_.scale);
        }
        if (j.contains("flipX")) { SetFlipX(j["flipX"].get<bool>()); }
        if (j.contains("flipY")) { SetFlipY(j["flipY"].get<bool>()); }
    }

    void SpriteObject::Draw(const Camera* camera) {
        // RenderGraph を経由しない直接呼び出し（レガシー経路）。
        // DrawViewInfo を持たないため、ここだけがコマンドリストの供給点になる。
        Draw2D(camera,
            spriteRenderer_ ? spriteRenderer_->GetDirectXCommon()->GetCommandList() : nullptr);
    }

    void SpriteObject::Draw(const DrawViewInfo& view) {
        Draw2D(view.GetCamera(), view.cmdList);
    }
}
