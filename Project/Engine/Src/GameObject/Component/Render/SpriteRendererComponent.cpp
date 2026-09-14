#include "pch.h"
#include "SpriteRendererComponent.h"

#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/EulerTransformComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/JsonManager/JsonManager.h"

#include <utility>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

COMPONENT_REGISTER(CoreEngine::SpriteRendererComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    SpriteRendererComponent::SpriteRendererComponent(std::string texturePath)
        : texturePath_(std::move(texturePath))
    {
    }

    SpriteRendererComponent::~SpriteRendererComponent() = default;

    void SpriteRendererComponent::Awake()
    {
        awoken_ = true;

        // 位置・回転・スケールは兄弟のトランスフォームから取る。無ければ 2D 用の軽量なものを足す
        if (GameObject* owner = GetOwner()) {
            transform_ = owner->GetComponent<ITransformSource>();
            if (!transform_) {
                transform_ = owner->AddComponent<EulerTransformComponent>();
            }
        }

        ResolveRenderer();
        CreateGpuResources();

        if (!texturePath_.empty()) {
            LoadTexture(texturePath_);
        }

        ApplyRenderOrder();
    }

    void SpriteRendererComponent::ResolveRenderer()
    {
        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
        if (renderManager) {
            spriteRenderer_ = dynamic_cast<SpriteRenderer*>(renderManager->GetRenderer(RenderPassType::Sprite));
        }
    }

    void SpriteRendererComponent::CreateGpuResources()
    {
        if (!spriteRenderer_) { return; }

        GraphicsCore* dxCommon = spriteRenderer_->GetGraphicsCore();
        ResourceFactory* resourceFactory = spriteRenderer_->GetResourceFactory();
        if (!dxCommon || !resourceFactory) { return; }

        // 頂点バッファ（4 頂点のクワッド）
        vertexResource_ = resourceFactory->CreateBufferResource(
            dxCommon->GetDevice(),
            sizeof(VertexData) * 4);

        vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        // インデックスバッファ
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

        // マテリアル（色と UV 変換の定数バッファ）
        material_ = std::make_unique<SpriteMaterialInstance>();
        material_->Initialize(dxCommon->GetDevice());
        material_->SetColor(color_);
        material_->SetUVTransform(uvMatrix_);

        UpdateVertexData();
        vertexDataDirty_ = false;
    }

    void SpriteRendererComponent::LoadTexture(const std::string& texturePath)
    {
        auto& textureManager = TextureManager::GetInstance();
        textureHandle_ = textureManager.Load(texturePath);

        const DirectX::TexMetadata metadata = textureManager.GetMetadata(texturePath);
        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);
    }

    void SpriteRendererComponent::SetTexture(const std::string& texturePath)
    {
        texturePath_ = texturePath;
        if (texturePath_.empty()) {
            textureHandle_ = {};
            textureSize_ = { 1.0f, 1.0f };
            return;
        }

        // Awake より前はパスだけを控え、Awake で読み込む
        if (awoken_ && TextureManager::GetInstance().IsInitialized()) {
            LoadTexture(texturePath_);
        }
    }

    Vector2 SpriteRendererComponent::GetActualSize() const
    {
        const ITransformSource* transform = GetTransformSource();
        if (!transform) {
            return textureSize_;
        }
        const Vector3& scale = const_cast<ITransformSource*>(transform)->Scale();
        return { textureSize_.x * scale.x, textureSize_.y * scale.y };
    }

    void SpriteRendererComponent::UpdateVertexData()
    {
        if (!vertexResource_) { return; }

        VertexData* vertexData = nullptr;
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

        // アンカーポイントを考慮したローカル座標
        const float left = -anchorPoint_.x;
        const float right = 1.0f - anchorPoint_.x;
        const float top = anchorPoint_.y;
        const float bottom = anchorPoint_.y - 1.0f;

        // フリップを考慮した UV 範囲
        const float uMin = flipX_ ? uvMax_.x : uvMin_.x;
        const float uMax = flipX_ ? uvMin_.x : uvMax_.x;
        const float vMin = flipY_ ? uvMax_.y : uvMin_.y;
        const float vMax = flipY_ ? uvMin_.y : uvMax_.y;

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

    void SpriteRendererComponent::Update()
    {
        const GameObject* owner = GetOwner();
        if (!owner || !owner->IsActive()) { return; }

        if (animator_ && animator_->IsPlaying()) {
            animator_->Update(Time::DeltaTime(), this);
        }
    }

    SpriteAnimator& SpriteRendererComponent::GetAnimator()
    {
        if (!animator_) {
            animator_ = std::make_unique<SpriteAnimator>();
        }
        return *animator_;
    }

    ITransformSource* SpriteRendererComponent::GetTransformSource() const
    {
        if (!transform_) {
            transform_ = Sibling<ITransformSource>();
        }
        return transform_;
    }

    void SpriteRendererComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* owner = GetOwner();
        if (!owner || !owner->IsActive()) { return; }

        Draw2D(view.GetCamera(), view.cmdList);
    }

    void SpriteRendererComponent::Draw2D(const Camera* camera, ID3D12GraphicsCommandList* commandList)
    {
        // テクスチャが無いまま SRV を差すとデバッグレイヤーが止めるので描かない
        if (!spriteRenderer_ || !commandList || !material_ || textureHandle_.gpuHandle.ptr == 0) {
            return;
        }

        ITransformSource* transform = GetTransformSource();
        if (!transform) { return; }

        if (vertexDataDirty_) {
            UpdateVertexData();
            vertexDataDirty_ = false;
        }

        size_t bufferIndex = spriteRenderer_->GetAvailableConstantBuffer();

        const Vector3& translate = transform->Translate();
        const Vector3& rotate = transform->Rotate();
        const Vector3& scale = transform->Scale();

        // 実際の描画サイズ（テクスチャサイズ × スケール）
        const Vector3 actualScale = {
            textureSize_.x * scale.x,
            textureSize_.y * scale.y,
            scale.z
        };

        auto& transformData = spriteRenderer_->GetTransformDataPool()[bufferIndex];
        transformData->WVP = spriteRenderer_->CalculateWVPMatrix(translate, actualScale, rotate, camera);
        transformData->world = Matrix::MakeAffine(actualScale, rotate, translate);

        // 定数バッファ（シェーダーリフレクションから取得したインデックスを使う）
        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("gMaterial"),
            material_->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("TransformationMatrix"),
            spriteRenderer_->GetTransformResource(bufferIndex)->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(
            spriteRenderer_->GetRootParamIndex("gTexture"),
            textureHandle_.gpuHandle);

        commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commandList->IASetIndexBuffer(&indexBufferView_);

        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    void SpriteRendererComponent::SetColor(const Vector4& color)
    {
        color_ = color;
        if (material_) {
            material_->SetColor(color_);
        }
    }

    void SpriteRendererComponent::SetUVTransform(const Matrix4x4& uvTransform)
    {
        uvMatrix_ = uvTransform;
        if (material_) {
            material_->SetUVTransform(uvMatrix_);
        }
    }

    void SpriteRendererComponent::SetTextureRect(float texLeft, float texTop, float texWidth, float texHeight,
        const std::string& texturePath)
    {
        const DirectX::TexMetadata metadata = TextureManager::GetInstance().GetMetadata(texturePath);
        const float textureWidth = static_cast<float>(metadata.width);
        const float textureHeight = static_cast<float>(metadata.height);
        if (textureWidth <= 0.0f || textureHeight <= 0.0f) { return; }

        uvMin_.x = texLeft / textureWidth;
        uvMin_.y = texTop / textureHeight;
        uvMax_.x = (texLeft + texWidth) / textureWidth;
        uvMax_.y = (texTop + texHeight) / textureHeight;

        vertexDataDirty_ = true;
    }

    void SpriteRendererComponent::SetUVRect(float uvLeft, float uvTop, float uvRight, float uvBottom)
    {
        uvMin_.x = uvLeft;
        uvMin_.y = uvTop;
        uvMax_.x = uvRight;
        uvMax_.y = uvBottom;

        vertexDataDirty_ = true;
    }

    void SpriteRendererComponent::SetUVOffset(float offsetX, float offsetY)
    {
        uvTransform_.translate = { offsetX, offsetY, 0.0f };
        UpdateUVTransformMatrix();
    }

    void SpriteRendererComponent::SetUVScale(float scaleX, float scaleY)
    {
        uvTransform_.scale = { scaleX, scaleY, 1.0f };
        UpdateUVTransformMatrix();
    }

    void SpriteRendererComponent::SetUVRotation(float rotation)
    {
        uvTransform_.rotate.z = rotation;
        UpdateUVTransformMatrix();
    }

    void SpriteRendererComponent::ResetUVTransform()
    {
        uvTransform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
        SetUVTransform(Matrix::Identity());
    }

    void SpriteRendererComponent::UpdateUVTransformMatrix()
    {
        const Matrix4x4 scaleMatrix = Matrix::Scale(uvTransform_.scale);
        const Matrix4x4 rotateMatrix = Matrix::RotationZ(uvTransform_.rotate.z);
        const Matrix4x4 translateMatrix = Matrix::Translation(uvTransform_.translate);
        SetUVTransform(scaleMatrix * rotateMatrix * translateMatrix);
    }

    void SpriteRendererComponent::SetAnchor(const Vector2& anchor)
    {
        if (anchorPoint_.x != anchor.x || anchorPoint_.y != anchor.y) {
            anchorPoint_ = anchor;
            vertexDataDirty_ = true;
        }
    }

    void SpriteRendererComponent::SetFlipX(bool flip)
    {
        if (flipX_ != flip) {
            flipX_ = flip;
            vertexDataDirty_ = true;
        }
    }

    void SpriteRendererComponent::SetFlipY(bool flip)
    {
        if (flipY_ != flip) {
            flipY_ = flip;
            vertexDataDirty_ = true;
        }
    }

    void SpriteRendererComponent::SetSortingLayer(int layer)
    {
        sortingLayer_ = layer;
        hasRenderOrder_ = true;
        ApplyRenderOrder();
    }

    void SpriteRendererComponent::SetOrderInLayer(int order)
    {
        orderInLayer_ = order;
        hasRenderOrder_ = true;
        ApplyRenderOrder();
    }

    void SpriteRendererComponent::ApplyRenderOrder()
    {
        if (!hasRenderOrder_) { return; }
        if (GameObject* owner = GetOwner()) {
            owner->SetRenderOrder(sortingLayer_ * 1000 + orderInLayer_);
        }
    }

    void SpriteRendererComponent::Reset()
    {
        SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        ResetUVTransform();
        anchorPoint_ = { 0.5f, 0.5f };
        uvMin_ = { 0.0f, 0.0f };
        uvMax_ = { 1.0f, 1.0f };
        flipX_ = false;
        flipY_ = false;

        vertexDataDirty_ = true;
    }

    json SpriteRendererComponent::OnSerialize() const
    {
        json j = json::object();

        if (!texturePath_.empty()) {
            j["texture"] = AssetPathToJson(texturePath_);
        }
        j["color"] = JsonManager::Vector4ToJson(color_);
        j["anchor"]["x"] = anchorPoint_.x;
        j["anchor"]["y"] = anchorPoint_.y;
        j["uvMin"]["x"] = uvMin_.x;
        j["uvMin"]["y"] = uvMin_.y;
        j["uvMax"]["x"] = uvMax_.x;
        j["uvMax"]["y"] = uvMax_.y;
        j["uvOffset"]["x"] = uvTransform_.translate.x;
        j["uvOffset"]["y"] = uvTransform_.translate.y;
        j["uvScale"]["x"] = uvTransform_.scale.x;
        j["uvScale"]["y"] = uvTransform_.scale.y;
        j["uvRotation"] = uvTransform_.rotate.z;
        j["flipX"] = flipX_;
        j["flipY"] = flipY_;
        j["blendMode"] = static_cast<int>(blendMode_);
        if (hasRenderOrder_) {
            j["sortingLayer"] = sortingLayer_;
            j["orderInLayer"] = orderInLayer_;
        }

        return j;
    }

    void SpriteRendererComponent::OnDeserialize(const json& j)
    {
        if (!j.is_object()) { return; }

        if (const auto it = j.find("texture"); it != j.end()) {
            const GameObject* owner = GetOwner();
            const std::string context = "SpriteRenderer（" + (owner ? owner->GetName() : std::string{}) + "）のテクスチャ";
            SetTexture(JsonToAssetPath(*it, context));
        }

        SetColor(JsonManager::SafeGetVector4(j, "color", color_));

        if (j.contains("anchor")) {
            SetAnchor({
                JsonManager::SafeGet<float>(j["anchor"], "x", anchorPoint_.x),
                JsonManager::SafeGet<float>(j["anchor"], "y", anchorPoint_.y) });
        }
        if (j.contains("uvMin") && j.contains("uvMax")) {
            SetUVRect(
                JsonManager::SafeGet<float>(j["uvMin"], "x", uvMin_.x),
                JsonManager::SafeGet<float>(j["uvMin"], "y", uvMin_.y),
                JsonManager::SafeGet<float>(j["uvMax"], "x", uvMax_.x),
                JsonManager::SafeGet<float>(j["uvMax"], "y", uvMax_.y));
        }

        if (j.contains("uvOffset") || j.contains("uvScale") || j.contains("uvRotation")) {
            if (j.contains("uvOffset")) {
                uvTransform_.translate.x = JsonManager::SafeGet<float>(j["uvOffset"], "x", uvTransform_.translate.x);
                uvTransform_.translate.y = JsonManager::SafeGet<float>(j["uvOffset"], "y", uvTransform_.translate.y);
            }
            if (j.contains("uvScale")) {
                uvTransform_.scale.x = JsonManager::SafeGet<float>(j["uvScale"], "x", uvTransform_.scale.x);
                uvTransform_.scale.y = JsonManager::SafeGet<float>(j["uvScale"], "y", uvTransform_.scale.y);
            }
            uvTransform_.rotate.z = JsonManager::SafeGet<float>(j, "uvRotation", uvTransform_.rotate.z);
            UpdateUVTransformMatrix();
        }

        if (j.contains("flipX")) { SetFlipX(JsonManager::SafeGet<bool>(j, "flipX", flipX_)); }
        if (j.contains("flipY")) { SetFlipY(JsonManager::SafeGet<bool>(j, "flipY", flipY_)); }

        if (const auto it = j.find("blendMode"); it != j.end() && it->is_number_integer()) {
            const int index = it->get<int>();
            if (index >= 0 && index < static_cast<int>(kBlendModeCount)) {
                blendMode_ = static_cast<BlendMode>(index);
            }
        }

        if (j.contains("sortingLayer") || j.contains("orderInLayer")) {
            sortingLayer_ = JsonManager::SafeGet<int>(j, "sortingLayer", sortingLayer_);
            orderInLayer_ = JsonManager::SafeGet<int>(j, "orderInLayer", orderInLayer_);
            hasRenderOrder_ = true;
            ApplyRenderOrder();
        }
    }

#ifdef USE_IMGUI
    bool SpriteRendererComponent::DrawInspector()
    {
        bool changed = false;

        // ── テクスチャ（ProjectView からのドロップで差し替える） ──
        UI::SectionHeader("テクスチャ");
        ImGui::Button(texturePath_.empty() ? "テクスチャをドロップ" : texturePath_.c_str(),
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_FILE")) {
                SetTexture(static_cast<const char*>(payload->Data));
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Text("テクスチャ: %.0f x %.0f px", textureSize_.x, textureSize_.y);
        const Vector2 actualSize = GetActualSize();
        ImGui::Text("描画サイズ: %.0f x %.0f px", actualSize.x, actualSize.y);

        // ── 色と UV ──────────────────────────────────────
        UI::SectionHeader("基本設定");
        Vector4 color = color_;
        if (UI::ColorEdit("カラー", color)) {
            SetColor(color);
            changed = true;
        }

        UI::SectionHeader("UV 変換");
        bool uvChanged = false;
        uvChanged |= ImGui::DragFloat2("オフセット##UV", &uvTransform_.translate.x, 0.01f);
        uvChanged |= ImGui::DragFloat2("スケール##UV", &uvTransform_.scale.x, 0.01f, 0.01f, 10.0f);
        uvChanged |= UI::SliderFloat("回転##UV", uvTransform_.rotate.z, -MathCore::Constants::kPi, MathCore::Constants::kPi);
        if (uvChanged) {
            UpdateUVTransformMatrix();
            changed = true;
        }
        if (ImGui::Button("UV リセット")) {
            ResetUVTransform();
            changed = true;
        }

        // ── ブレンド・描画順・フリップ・アンカー ────────────────
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
            SetAnchor(anchorTemp);
            changed = true;
        }
        if (ImGui::Button("TL##anchor")) { SetAnchor({ 0.0f, 0.0f }); changed = true; } UI::SameLine();
        if (ImGui::Button("TC##anchor")) { SetAnchor({ 0.5f, 0.0f }); changed = true; } UI::SameLine();
        if (ImGui::Button("TR##anchor")) { SetAnchor({ 1.0f, 0.0f }); changed = true; } UI::SameLine();
        if (ImGui::Button("C##anchor")) { SetAnchor({ 0.5f, 0.5f }); changed = true; } UI::SameLine();
        if (ImGui::Button("BL##anchor")) { SetAnchor({ 0.0f, 1.0f }); changed = true; } UI::SameLine();
        if (ImGui::Button("BR##anchor")) { SetAnchor({ 1.0f, 1.0f }); changed = true; }

        UI::Spacing();
        if (ImGui::Button("リセット##sprite")) {
            Reset();
            changed = true;
        }

        return changed;
    }
#endif // USE_IMGUI
}
