#include "pch.h"
#include "UI/UIImageComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "UI/RectTransformComponent.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cstring>

REFLECT_REGISTER(CoreEngine::UIImageComponent)
COMPONENT_REGISTER(CoreEngine::UIImageComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        /// @brief テクスチャを指していないときに貼る白
        constexpr const char* kWhiteTexture = "white1x1.png";
    }

    UIImageComponent::~UIImageComponent() = default;

    void UIImageComponent::Awake()
    {
        awoken_ = true;

        // 配置は兄弟の UI トランスフォームから取る。無ければ足す
        if (GameObject* const owner = GetOwner()) {
            rect_ = owner->GetOrAddComponent<RectTransformComponent>();
        }

        ResolveRenderer();
        CreateGpuResources();
        LoadTexture();
        materialSlot_.Resolve();
    }

    void UIImageComponent::ResolveRenderer()
    {
        GameObject* const owner = GetOwner();
        EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
        if (renderManager) {
            renderer_ = dynamic_cast<UIRenderer*>(renderManager->GetRenderer(RenderPassType::UI));
        }
    }

    void UIImageComponent::CreateGpuResources()
    {
        if (!renderer_) { return; }

        GraphicsCore* const graphics = renderer_->GetGraphicsCore();
        if (!graphics) { return; }

        // マテリアル（色の定数バッファ）
        material_ = std::make_unique<UIMaterialInstance>();
        material_->Initialize(*graphics);
        material_->SetColor(color_);
    }

    void UIImageComponent::LoadTexture()
    {
        auto& textureManager = TextureManager::GetInstance();
        if (!textureManager.IsInitialized()) { return; }

        const std::string path = textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string(kWhiteTexture);
        textureHandle_ = textureManager.Load(path);

        const DirectX::TexMetadata metadata = textureManager.GetMetadata(path);
        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);
    }

    void UIImageComponent::SetTexture(const std::string& texturePath)
    {
        const Reflection::AssetRefValue previous = textureAsset_.GetValue();
        textureAsset_.SetPath(texturePath);

        // Awake より前は指す先だけを控え、Awake で読み込む。指す先が変わらなければ読み直さない
        if (awoken_ && !(textureAsset_.GetValue() == previous)) {
            LoadTexture();
        }
    }

    std::string UIImageComponent::GetTexturePath() const
    {
        return textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string();
    }

    void UIImageComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) {
            return;
        }
        textureAsset_.SetValue(value);
        if (awoken_) {
            LoadTexture();
        }
    }

    void UIImageComponent::SetNativeSize()
    {
        RectTransformComponent* const rect = GetRectTransform();
        if (rect && textureSize_.x > 0.0f && textureSize_.y > 0.0f) {
            rect->SetSize({ textureSize_.x / static_cast<float>(columns_), textureSize_.y / static_cast<float>(rows_) });
        }
    }

    void UIImageComponent::SetColumns(int columns)
    {
        columns_ = (std::max)(columns, 1);
        verticesDirty_ = true;
    }

    void UIImageComponent::SetRows(int rows)
    {
        rows_ = (std::max)(rows, 1);
        verticesDirty_ = true;
    }

    void UIImageComponent::SetFrame(int frame)
    {
        if (frame_ != frame) {
            frame_ = frame;
            verticesDirty_ = true;
        }
    }

    void UIImageComponent::SetMaterial(const std::string& materialPath)
    {
        Reflection::AssetRefValue value;
        if (!materialPath.empty()) {
            AssetRef<ShaderMaterialAsset> reference(materialPath);
            value = reference.GetValue();
        }
        SetMaterialAsset(value);
    }

    void UIImageComponent::SetMaterialAsset(const Reflection::AssetRefValue& value)
    {
        if (materialSlot_.SetAsset(value) && awoken_) {
            materialSlot_.Resolve();
        }
    }

    void UIImageComponent::SetColor(const Vector4& color)
    {
        color_ = color;
        if (material_) {
            material_->SetColor(color_);
        }
    }

    bool UIImageComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RectTransformComponent*>(&other) != nullptr;
    }

    RectTransformComponent* UIImageComponent::GetRectTransform() const
    {
        if (!rect_) {
            rect_ = Sibling<RectTransformComponent>();
        }
        return rect_;
    }

    void UIImageComponent::UpdateVertexData(const Vector2& pivot)
    {
        VertexData* const vertexData = vertices_.data();

        // 大きさ 1 の矩形を、基準点が原点に来るようにずらす（Y 下正）
        const float left = -pivot.x;
        const float right = 1.0f - pivot.x;
        const float top = -pivot.y;
        const float bottom = 1.0f - pivot.y;

        // 今のコマの UV の範囲（範囲外のコマは端へ丸める）
        const int frame = std::clamp(frame_, 0, columns_ * rows_ - 1);
        const float cellWidth = 1.0f / static_cast<float>(columns_);
        const float cellHeight = 1.0f / static_cast<float>(rows_);
        const float uMin = cellWidth * static_cast<float>(frame % columns_);
        const float vMin = cellHeight * static_cast<float>(frame / columns_);
        const float uMax = uMin + cellWidth;
        const float vMax = vMin + cellHeight;

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

        builtPivot_ = pivot;
        verticesDirty_ = false;
    }

    void UIImageComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        if (!owner || !owner->IsActive()) { return; }

        // テクスチャが無いまま SRV を差すとデバッグレイヤーが止めるので描かない
        ID3D12GraphicsCommandList* const commandList = view.cmdList;
        const RectTransformComponent* const rect = GetRectTransform();
        if (!commandList || !renderer_ || !material_ || !rect || textureHandle_.gpuHandle.ptr == 0) {
            return;
        }

        const UILayout& layout = rect->GetLayout();
        if (verticesDirty_ || layout.pivot.x != builtPivot_.x || layout.pivot.y != builtPivot_.y) {
            UpdateVertexData(layout.pivot);
        }

        UploadRing& uploadRing = renderer_->GetGraphicsCore()->GetUploadRing();

        // 頂点は描くたびに、このフレームの UploadRing に置く
        const UploadAllocation vertices = uploadRing.Allocate(static_cast<uint32_t>(sizeof(vertices_)), 16);
        if (!vertices.IsValid()) { return; }
        std::memcpy(vertices.cpu, vertices_.data(), sizeof(vertices_));

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = vertices.gpuAddress;
        vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(vertices_));
        vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(VertexData));

        const Vector2 screenPosition = layout.CalculateScreenPosition(renderer_->GetScreenSize());
        const Vector3 position = { screenPosition.x, screenPosition.y, 0.0f };
        const Vector3 scale = { layout.size.x, layout.size.y, 1.0f };
        const Vector3 rotation = { 0.0f, 0.0f, layout.rotation };

        const size_t bufferIndex = renderer_->GetAvailableConstantBuffer();
        UIRenderer::TransformationMatrix* transformData = renderer_->GetTransformData(bufferIndex);
        transformData->WVP = renderer_->CalculateWVPMatrix(position, scale, rotation);
        transformData->world = Matrix::MakeAffine(scale, rotation, position);
        const D3D12_GPU_VIRTUAL_ADDRESS transformAddress = renderer_->GetTransformGpuAddress(bufferIndex);

        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        commandList->IASetIndexBuffer(&renderer_->GetQuadIndexBufferView());

        if (materialSlot_.IsSet() && RenderWithMaterial(commandList, uploadRing, transformAddress)) {
            commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
            return;
        }

        renderer_->BindPipeline(commandList, nullptr);

        // マテリアルを指しているのに描けないときは、気づけるようマゼンタで描く
        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = material_->GetGPUVirtualAddress();
        if (materialSlot_.IsSet()) {
            SpriteMaterialData fallback{};
            fallback.color = { 1.0f, 0.0f, 1.0f, 1.0f };
            fallback.uvTransform = Matrix::Identity();
            materialAddress = uploadRing.AllocateConstants(fallback);
            if (materialAddress == 0) { return; }
        }

        commandList->SetGraphicsRootConstantBufferView(
            renderer_->GetRootParamIndex("gMaterial"),
            materialAddress);
        commandList->SetGraphicsRootConstantBufferView(
            renderer_->GetRootParamIndex("TransformationMatrix"),
            transformAddress);
        commandList->SetGraphicsRootDescriptorTable(
            renderer_->GetRootParamIndex("gTexture"),
            textureHandle_.gpuHandle);

        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }

    bool UIImageComponent::RenderWithMaterial(ID3D12GraphicsCommandList* commandList, UploadRing& uploadRing,
        D3D12_GPU_VIRTUAL_ADDRESS transformAddress)
    {
        const ShaderMaterial* const material = materialSlot_.GetMaterial();
        const ShaderMaterialPipeline* const pipeline = material ? renderer_->GetMaterialPipeline(*material) : nullptr;
        if (!pipeline) {
            return false;
        }

        SpriteDrawData draw{};
        draw.color = color_;
        draw.textureSize = textureSize_;
        draw.time = Time::TimeSinceStartup();
        draw.unscaledTime = Time::UnscaledTimeSinceStartup();
        draw.deltaTime = Time::DeltaTime();
        const D3D12_GPU_VIRTUAL_ADDRESS drawAddress = uploadRing.AllocateConstants(draw);
        if (drawAddress == 0) {
            return false;
        }

        renderer_->BindPipeline(commandList, pipeline);
        if (!materialSlot_.Bind(commandList, *pipeline, uploadRing, transformAddress, drawAddress, textureHandle_.gpuHandle)) {
            renderer_->BindPipeline(commandList, nullptr);
            return false;
        }
        return true;
    }
}
