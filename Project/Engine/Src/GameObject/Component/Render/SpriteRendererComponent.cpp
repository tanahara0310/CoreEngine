#include "pch.h"
#include "SpriteRendererComponent.h"

#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
    /// @brief ブレンドの名前（`BlendMode` の並び）
    constexpr const char* kBlendModeNames[] = { "なし", "アルファ", "加算", "減算", "乗算", "スクリーン" };
    static_assert(std::size(kBlendModeNames) == CoreEngine::kBlendModeCount);

    /// @brief フィルタの名前（`SpriteFilter` の並び）
    constexpr const char* kFilterNames[] = { "線形", "ポイント" };

    /// @brief 1 単位のピクセル数の下限（0 で割らないため）
    constexpr float kMinPixelsPerUnit = 0.01f;
}

REFLECT_DEFINE_BEGIN(CoreEngine::SpriteRendererComponent, "スプライト描画")
    REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
        p.assetType = ::CoreEngine::AssetType::Texture)
    REFLECT_ACCESSOR("color", "カラー", GetColor, SetColor,
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_ACCESSOR("material", "マテリアル", GetMaterialAsset, SetMaterialAsset,
        p.assetType = ::CoreEngine::AssetType::Material,
        p.tooltip = "描き方を決めるシェーダと値（.mat）。指さなければテクスチャ × カラーで描く")
    REFLECT_ACCESSOR("pixelsPerUnit", "1 単位のピクセル数", GetPixelsPerUnit, SetPixelsPerUnit,
        p.range = Range(kMinPixelsPerUnit, 100000.0f, 1.0f),
        p.tooltip = "テクスチャの何ピクセルを 1 単位（1m）にするか。大きいほど小さく描く")
    REFLECT_ENUM_ACCESSOR("filter", "フィルタ", GetFilter, SetFilter, kFilterNames,
        p.tooltip = "ポイントは拡大してもにじまない（ドット絵向け）")
    REFLECT_ACCESSOR("anchor", "アンカー", GetAnchor, SetAnchor,
        p.range = Range(0.0f, 1.0f, 0.01f), p.tooltip = "位置に合わせる点。(0, 0) が左上、(1, 1) が右下")
    REFLECT_ACCESSOR("uvMin", "UV 左上", GetUVMin, SetUVMin,
        p.range = Range(0.0f, 1.0f, 0.001f), p.tooltip = "描く範囲。板の大きさもこの範囲のピクセル数で決まる")
    REFLECT_ACCESSOR("uvMax", "UV 右下", GetUVMax, SetUVMax,
        p.range = Range(0.0f, 1.0f, 0.001f))
    REFLECT_ACCESSOR("columns", "横のコマ数", GetColumns, SetColumns,
        p.range = Range(1.0f, 256.0f, 0.05f), p.tooltip = "描く範囲を横に何コマへ分けるか（スプライトシート用）")
    REFLECT_ACCESSOR("rows", "縦のコマ数", GetRows, SetRows,
        p.range = Range(1.0f, 256.0f, 0.05f), p.tooltip = "描く範囲を縦に何コマへ分けるか")
    REFLECT_ACCESSOR("frame", "コマ", GetFrame, SetFrame,
        p.range = Range(0.0f, 65535.0f, 0.05f), p.tooltip = "描くコマ。左上から右へ、行の終わりで次の行へ数えて 0 から")
    REFLECT_ACCESSOR("uvOffset", "UV の移動", GetUVOffset, SetUVOffset, p.range = Speed(0.01f))
    REFLECT_ACCESSOR("uvScale", "UV の倍率", GetUVScale, SetUVScale, p.range = Speed(0.01f))
    REFLECT_ACCESSOR("uvRotation", "UV の回転", GetUVRotation, SetUVRotation,
        p.range = Speed(0.01f), p.displayScale = kDegreesPerRadian)
    REFLECT_ACCESSOR("flipX", "左右反転", GetFlipX, SetFlipX)
    REFLECT_ACCESSOR("flipY", "上下反転", GetFlipY, SetFlipY)
    REFLECT_ENUM_ACCESSOR("blendMode", "ブレンド", GetBlendMode, SetBlendMode, kBlendModeNames)
    REFLECT_ACCESSOR("sortingLayer", "ソートレイヤー", GetSortingLayer, SetSortingLayer,
        p.range = Range(-100.0f, 100.0f, 0.1f), p.tooltip = "大きいほど手前に描く")
    REFLECT_ACCESSOR("orderInLayer", "レイヤー内の順", GetOrderInLayer, SetOrderInLayer,
        p.range = Range(-9999.0f, 9999.0f, 0.1f), p.tooltip = "同じレイヤーの中で、大きいほど手前に描く")
    REFLECT_READONLY_ACCESSOR("textureSize", "テクスチャの大きさ", GetTextureSize)
    REFLECT_READONLY_ACCESSOR("size", "板の大きさ", GetSize)
    REFLECT_METHOD("SetMaterialFloat", "マテリアルの値を上書き（数）", SetMaterialFloat)
    REFLECT_METHOD("SetMaterialInt", "マテリアルの値を上書き（整数）", SetMaterialInt)
    REFLECT_METHOD("SetMaterialVector", "マテリアルの値を上書き（ベクトル）", SetMaterialVector)
    REFLECT_METHOD("SetMaterialTexture", "マテリアルのテクスチャを上書き", SetMaterialTexture)
    REFLECT_METHOD("ClearMaterialOverrides", "マテリアルの上書きをやめる", ClearMaterialOverrides)
    REFLECT_METHOD("GetMaterialFloat", "マテリアルの値（数）", GetMaterialFloat)
    REFLECT_METHOD("GetMaterialVector", "マテリアルの値（ベクトル）", GetMaterialVector)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::SpriteRendererComponent)
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

        if (GameObject* owner = GetOwner()) {
            transform_ = owner->GetOrAddComponent<TransformComponent>();
        }

        ResolveRenderer();
        CreateGpuResources();

        if (!texturePath_.empty()) {
            LoadTexture(texturePath_);
        }
        materialSlot_.Resolve();
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
        if (!dxCommon) { return; }

        // インデックスバッファ
        indexResource_ = ResourceFactory::CreateBufferResource(
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

        // マテリアル（色・UV 変換・フィルタの定数バッファ）
        material_ = std::make_unique<SpriteMaterialInstance>();
        material_->Initialize(dxCommon->GetDevice());
        material_->SetColor(color_);
        material_->SetUVTransform(uvMatrix_);
        material_->SetFilterMode(static_cast<uint32_t>(filter_));

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

    void SpriteRendererComponent::SetPixelsPerUnit(float pixelsPerUnit)
    {
        pixelsPerUnit_ = (std::max)(pixelsPerUnit, kMinPixelsPerUnit);
    }

    Vector2 SpriteRendererComponent::GetSize() const
    {
        return {
            std::abs(uvMax_.x - uvMin_.x) / static_cast<float>(columns_) * textureSize_.x / pixelsPerUnit_,
            std::abs(uvMax_.y - uvMin_.y) / static_cast<float>(rows_) * textureSize_.y / pixelsPerUnit_
        };
    }

    void SpriteRendererComponent::SetColumns(int columns)
    {
        columns_ = (std::max)(columns, 1);
        vertexDataDirty_ = true;
    }

    void SpriteRendererComponent::SetRows(int rows)
    {
        rows_ = (std::max)(rows, 1);
        vertexDataDirty_ = true;
    }

    void SpriteRendererComponent::SetFrame(int frame)
    {
        if (frame_ != frame) {
            frame_ = frame;
            vertexDataDirty_ = true;
        }
    }

    void SpriteRendererComponent::GetFrameUVRect(Vector2& outMin, Vector2& outMax) const
    {
        // 範囲外のコマは端へ丸める
        const int frame = std::clamp(frame_, 0, GetFrameCount() - 1);
        const int column = frame % columns_;
        const int row = frame / columns_;
        const float cellWidth = (uvMax_.x - uvMin_.x) / static_cast<float>(columns_);
        const float cellHeight = (uvMax_.y - uvMin_.y) / static_cast<float>(rows_);
        outMin = { uvMin_.x + cellWidth * static_cast<float>(column), uvMin_.y + cellHeight * static_cast<float>(row) };
        outMax = { outMin.x + cellWidth, outMin.y + cellHeight };
    }

    void SpriteRendererComponent::SetFilter(SpriteFilter filter)
    {
        filter_ = filter;
        if (material_) {
            material_->SetFilterMode(static_cast<uint32_t>(filter_));
        }
    }

    void SpriteRendererComponent::UpdateVertexData()
    {
        VertexData* const vertexData = vertices_.data();

        // アンカーポイントを考慮したローカル座標
        const float left = -anchorPoint_.x;
        const float right = 1.0f - anchorPoint_.x;
        const float top = anchorPoint_.y;
        const float bottom = anchorPoint_.y - 1.0f;

        // 今のコマの UV 範囲（フリップを考慮）
        Vector2 cellMin{};
        Vector2 cellMax{};
        GetFrameUVRect(cellMin, cellMax);
        const float uMin = flipX_ ? cellMax.x : cellMin.x;
        const float uMax = flipX_ ? cellMin.x : cellMax.x;
        const float vMin = flipY_ ? cellMax.y : cellMin.y;
        const float vMax = flipY_ ? cellMin.y : cellMax.y;

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
    }

    bool SpriteRendererComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    TransformComponent* SpriteRendererComponent::GetTransform() const
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }

    Matrix4x4 SpriteRendererComponent::GetQuadWorldMatrix() const
    {
        const Vector2 size = GetSize();
        const Matrix4x4 sizeMatrix = Matrix::Scale({ size.x, size.y, 1.0f });
        const TransformComponent* transform = GetTransform();
        return transform ? sizeMatrix * transform->Get().GetWorldMatrix() : sizeMatrix;
    }

    std::array<Vector3, 4> SpriteRendererComponent::GetWorldCorners() const
    {
        // 頂点を書き直す前でも同じ形になるよう、アンカーから角を作る
        const float left = -anchorPoint_.x;
        const float right = 1.0f - anchorPoint_.x;
        const float top = anchorPoint_.y;
        const float bottom = anchorPoint_.y - 1.0f;

        const Matrix4x4 world = GetQuadWorldMatrix();
        return {
            CoordinateTransform::TransformCoord(Vector3{ left, bottom, 0.0f }, world),
            CoordinateTransform::TransformCoord(Vector3{ left, top, 0.0f }, world),
            CoordinateTransform::TransformCoord(Vector3{ right, bottom, 0.0f }, world),
            CoordinateTransform::TransformCoord(Vector3{ right, top, 0.0f }, world),
        };
    }

    void SpriteRendererComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* owner = GetOwner();
        if (!owner || !owner->IsActive() || !view.view || !view.cmdList) { return; }

        // テクスチャが無いまま SRV を差すとデバッグレイヤーが止めるので描かない
        if (!spriteRenderer_ || !material_ || textureHandle_.gpuHandle.ptr == 0) {
            return;
        }

        if (vertexDataDirty_) {
            UpdateVertexData();
            vertexDataDirty_ = false;
        }

        UploadRing& uploadRing = spriteRenderer_->GetGraphicsCore()->GetUploadRing();

        // 頂点は描くたびに、このフレームの UploadRing に置く
        const UploadAllocation vertices = uploadRing.Allocate(static_cast<uint32_t>(sizeof(vertices_)), 16);
        if (!vertices.IsValid()) { return; }
        std::memcpy(vertices.cpu, vertices_.data(), sizeof(vertices_));

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = vertices.gpuAddress;
        vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(vertices_));
        vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));

        SpriteRenderer::TransformationMatrix transformation{};
        transformation.world = GetQuadWorldMatrix();
        transformation.WVP = transformation.world * view.view->viewProjection;
        const D3D12_GPU_VIRTUAL_ADDRESS transformAddress = uploadRing.AllocateConstants(transformation);
        if (transformAddress == 0) { return; }

        ID3D12GraphicsCommandList* const commandList = view.cmdList;
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        commandList->IASetIndexBuffer(&indexBufferView_);

        if (materialSlot_.IsSet() && RenderWithMaterial(commandList, uploadRing, transformAddress)) {
            commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
            return;
        }

        spriteRenderer_->BindPipeline(commandList, nullptr);

        // マテリアルを指しているのに描けないときは、気づけるようマゼンタで描く
        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = material_->GetGPUVirtualAddress();
        if (materialSlot_.IsSet()) {
            SpriteMaterialData fallback{};
            fallback.color = { 1.0f, 0.0f, 1.0f, 1.0f };
            fallback.uvTransform = uvMatrix_;
            fallback.filterMode = static_cast<uint32_t>(filter_);
            materialAddress = uploadRing.AllocateConstants(fallback);
            if (materialAddress == 0) { return; }
        }

        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("gMaterial"),
            materialAddress);
        commandList->SetGraphicsRootConstantBufferView(
            spriteRenderer_->GetRootParamIndex("TransformationMatrix"),
            transformAddress);
        commandList->SetGraphicsRootDescriptorTable(
            spriteRenderer_->GetRootParamIndex("gTexture"),
            textureHandle_.gpuHandle);

        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    bool SpriteRendererComponent::RenderWithMaterial(ID3D12GraphicsCommandList* commandList, UploadRing& uploadRing,
        D3D12_GPU_VIRTUAL_ADDRESS transformAddress)
    {
        const ShaderMaterial* const material = materialSlot_.GetMaterial();
        const ShaderMaterialPipeline* const pipeline = material ? spriteRenderer_->GetMaterialPipeline(*material) : nullptr;
        if (!pipeline) {
            return false;
        }

        SpriteDrawData draw{};
        draw.color = color_;
        draw.uvTransform = uvMatrix_;
        draw.textureSize = textureSize_;
        draw.time = Time::TimeSinceStartup();
        draw.unscaledTime = Time::UnscaledTimeSinceStartup();
        draw.deltaTime = Time::DeltaTime();
        draw.filterMode = static_cast<uint32_t>(filter_);
        const D3D12_GPU_VIRTUAL_ADDRESS drawAddress = uploadRing.AllocateConstants(draw);
        if (drawAddress == 0) {
            return false;
        }

        spriteRenderer_->BindPipeline(commandList, pipeline);
        if (!materialSlot_.Bind(commandList, *pipeline, uploadRing, transformAddress, drawAddress, textureHandle_.gpuHandle)) {
            spriteRenderer_->BindPipeline(commandList, nullptr);
            return false;
        }
        return true;
    }

    void SpriteRendererComponent::SetMaterial(const std::string& materialPath)
    {
        Reflection::AssetRefValue value;
        if (!materialPath.empty()) {
            AssetRef<ShaderMaterialAsset> reference(materialPath);
            value = reference.GetValue();
        }
        SetMaterialAsset(value);
    }

    void SpriteRendererComponent::SetMaterialAsset(const Reflection::AssetRefValue& value)
    {
        if (materialSlot_.SetAsset(value) && awoken_) {
            materialSlot_.Resolve();
        }
    }

    void SpriteRendererComponent::SetMaterialFloat(const std::string& name, float value)
    {
        const float numbers[] = { value };
        materialSlot_.SetNumbers(name, numbers);
    }

    void SpriteRendererComponent::SetMaterialInt(const std::string& name, int value)
    {
        const float numbers[] = { static_cast<float>(value) };
        materialSlot_.SetNumbers(name, numbers);
    }

    void SpriteRendererComponent::SetMaterialVector(const std::string& name, const Vector4& value)
    {
        const float numbers[] = { value.x, value.y, value.z, value.w };
        materialSlot_.SetNumbers(name, numbers);
    }

    void SpriteRendererComponent::SetMaterialTexture(const std::string& name, const std::string& texturePath)
    {
        materialSlot_.SetTexture(name, texturePath);
    }

    void SpriteRendererComponent::ClearMaterialOverrides()
    {
        materialSlot_.ClearOverrides();
    }

    float SpriteRendererComponent::GetMaterialFloat(const std::string& name) const
    {
        const MaterialNumbers* const numbers = materialSlot_.FindNumbers(name);
        return (numbers && !numbers->empty()) ? numbers->front() : 0.0f;
    }

    Vector4 SpriteRendererComponent::GetMaterialVector(const std::string& name) const
    {
        Vector4 value = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (const MaterialNumbers* const numbers = materialSlot_.FindNumbers(name)) {
            float* const components[] = { &value.x, &value.y, &value.z, &value.w };
            for (size_t index = 0; index < numbers->size() && index < 4; ++index) {
                *components[index] = (*numbers)[index];
            }
        }
        return value;
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

    void SpriteRendererComponent::Reset()
    {
        SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        ResetUVTransform();
        anchorPoint_ = { 0.5f, 0.5f };
        uvMin_ = { 0.0f, 0.0f };
        uvMax_ = { 1.0f, 1.0f };
        flipX_ = false;
        flipY_ = false;
        columns_ = 1;
        rows_ = 1;
        frame_ = 0;

        vertexDataDirty_ = true;
    }

    Reflection::AssetRefValue SpriteRendererComponent::GetTextureAsset() const
    {
        return Reflection::AssetRefValue{ {}, texturePath_ };
    }

    void SpriteRendererComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value.path == texturePath_) {
            return;
        }
        SetTexture(value.path);
    }
}
