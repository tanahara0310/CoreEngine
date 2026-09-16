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
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "UI/RectTransformComponent.h"

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
        ResourceFactory* const factory = renderer_->GetResourceFactory();
        if (!graphics || !factory) { return; }

        // 頂点バッファ（4 頂点の矩形）
        vertexResource_ = factory->CreateBufferResource(graphics->GetDevice(), sizeof(VertexData) * 4);
        vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        // インデックスバッファ
        indexResource_ = factory->CreateBufferResource(graphics->GetDevice(), sizeof(uint32_t) * 6);
        uint32_t* indexData = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        indexData[0] = 0; indexData[1] = 1; indexData[2] = 2;
        indexData[3] = 1; indexData[4] = 3; indexData[5] = 2;
        indexResource_->Unmap(0, nullptr);

        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

        // マテリアル（色の定数バッファ）
        material_ = std::make_unique<UIMaterialInstance>();
        material_->Initialize(graphics->GetDevice());
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
        textureAsset_.SetPath(texturePath);

        // Awake より前は指す先だけを控え、Awake で読み込む
        if (awoken_) {
            LoadTexture();
        }
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
            rect->SetSize(textureSize_);
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
        if (!vertexResource_) { return; }

        VertexData* vertexData = nullptr;
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

        // 大きさ 1 の矩形を、基準点が原点に来るようにずらす（Y 下正）
        const float left = -pivot.x;
        const float right = 1.0f - pivot.x;
        const float top = -pivot.y;
        const float bottom = 1.0f - pivot.y;

        // 左下
        vertexData[0].position = { left,  bottom, 0.0f, 1.0f };
        vertexData[0].texcoord = { 0.0f, 1.0f };
        vertexData[0].normal = { 0.0f, 0.0f, -1.0f };

        // 左上
        vertexData[1].position = { left,  top,    0.0f, 1.0f };
        vertexData[1].texcoord = { 0.0f, 0.0f };
        vertexData[1].normal = { 0.0f, 0.0f, -1.0f };

        // 右下
        vertexData[2].position = { right, bottom, 0.0f, 1.0f };
        vertexData[2].texcoord = { 1.0f, 1.0f };
        vertexData[2].normal = { 0.0f, 0.0f, -1.0f };

        // 右上
        vertexData[3].position = { right, top,    0.0f, 1.0f };
        vertexData[3].texcoord = { 1.0f, 0.0f };
        vertexData[3].normal = { 0.0f, 0.0f, -1.0f };

        vertexResource_->Unmap(0, nullptr);
        builtPivot_ = pivot;
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
        if (layout.pivot.x != builtPivot_.x || layout.pivot.y != builtPivot_.y) {
            UpdateVertexData(layout.pivot);
        }

        const Vector2 screenPosition = layout.CalculateScreenPosition(renderer_->GetScreenSize());
        const Vector3 position = { screenPosition.x, screenPosition.y, 0.0f };
        const Vector3 scale = { layout.size.x, layout.size.y, 1.0f };
        const Vector3 rotation = { 0.0f, 0.0f, layout.rotation };

        const size_t bufferIndex = renderer_->GetAvailableConstantBuffer();
        auto& transformData = renderer_->GetTransformDataPool()[bufferIndex];
        transformData->WVP = renderer_->CalculateWVPMatrix(position, scale, rotation);
        transformData->world = Matrix::MakeAffine(scale, rotation, position);

        commandList->SetGraphicsRootConstantBufferView(
            renderer_->GetRootParamIndex("gMaterial"),
            material_->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(
            renderer_->GetRootParamIndex("TransformationMatrix"),
            renderer_->GetTransformResource(bufferIndex)->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(
            renderer_->GetRootParamIndex("gTexture"),
            textureHandle_.gpuHandle);

        commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
        commandList->IASetIndexBuffer(&indexBufferView_);
        commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
    }
}
