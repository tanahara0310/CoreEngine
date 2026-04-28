#include "UIImage.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Model/VertexData.h"
#include "EngineSystem/EngineSystem.h"

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void UIImage::Initialize(const std::string& textureFilePath, const std::string& name)
    {
        if (!name.empty()) {
            SetName(name);
        }

        auto* engine = GetEngineSystem();
        auto* renderManager = engine->GetComponent<RenderManager>();
        renderer_ = dynamic_cast<UIRenderer*>(renderManager->GetRenderer(RenderPassType::UI));

        textureHandle_ = TextureManager::GetInstance().Load(textureFilePath);
        DirectX::TexMetadata metadata = TextureManager::GetInstance().GetMetadata(textureFilePath);
        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);
        layout_.size = textureSize_;

        CreateVertexBuffer();

        material_ = std::make_unique<UIMaterialInstance>();
        material_->Initialize(renderer_->GetDirectXCommon()->GetDevice());
    }

    void UIImage::SetTexture(const std::string& textureFilePath)
    {
        textureHandle_ = TextureManager::GetInstance().Load(textureFilePath);
        DirectX::TexMetadata metadata = TextureManager::GetInstance().GetMetadata(textureFilePath);
        textureSize_.x = static_cast<float>(metadata.width);
        textureSize_.y = static_cast<float>(metadata.height);
    }

    void UIImage::SetColor(const Vector4& color)
    {
        if (material_) { material_->SetColor(color); }
    }

    Vector4 UIImage::GetColor() const
    {
        return material_ ? material_->GetColor() : Vector4{ 1, 1, 1, 1 };
    }

    void UIImage::CreateVertexBuffer()
    {
        if (!renderer_) { return; }

        DirectXCommon*   dxCommon = renderer_->GetDirectXCommon();
        ResourceFactory* factory  = renderer_->GetResourceFactory();
        if (!dxCommon || !factory) { return; }

        vertexResource_ = factory->CreateBufferResource(dxCommon->GetDevice(), sizeof(VertexData) * 4);
        vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes    = sizeof(VertexData) * 4;
        vertexBufferView_.StrideInBytes  = sizeof(VertexData);

        indexResource_ = factory->CreateBufferResource(dxCommon->GetDevice(), sizeof(uint32_t) * 6);
        uint32_t* indexData = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        indexData[0] = 0; indexData[1] = 1; indexData[2] = 2;
        indexData[3] = 1; indexData[4] = 3; indexData[5] = 2;
        indexResource_->Unmap(0, nullptr);

        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes    = sizeof(uint32_t) * 6;
        indexBufferView_.Format         = DXGI_FORMAT_R32_UINT;

        UpdateVertexData();
    }

    void UIImage::UpdateVertexData()
    {
        if (!vertexResource_) { return; }

        VertexData* vertexData = nullptr;
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

        float left   = -layout_.pivot.x;
        float right  =  1.0f - layout_.pivot.x;
        float top    = -layout_.pivot.y;
        float bottom =  1.0f - layout_.pivot.y;

        vertexData[0].position = { left,  bottom, 0.0f, 1.0f }; vertexData[0].texcoord = { 0.0f, 1.0f }; vertexData[0].normal = { 0.0f, 0.0f, -1.0f };
        vertexData[1].position = { left,  top,    0.0f, 1.0f }; vertexData[1].texcoord = { 0.0f, 0.0f }; vertexData[1].normal = { 0.0f, 0.0f, -1.0f };
        vertexData[2].position = { right, bottom, 0.0f, 1.0f }; vertexData[2].texcoord = { 1.0f, 1.0f }; vertexData[2].normal = { 0.0f, 0.0f, -1.0f };
        vertexData[3].position = { right, top,    0.0f, 1.0f }; vertexData[3].texcoord = { 1.0f, 0.0f }; vertexData[3].normal = { 0.0f, 0.0f, -1.0f };

        vertexResource_->Unmap(0, nullptr);
        lastPivot_     = layout_.pivot;
        rebuildVertex_ = false;
    }

    void UIImage::Draw(const ICamera* /*camera*/)
    {
        if (!IsActive() || !renderer_ || !material_) { return; }

        if (rebuildVertex_) { UpdateVertexData(); }

        Vector2 screenPos = layout_.CalculateScreenPosition(renderer_->GetScreenSize());

        auto* commandList = renderer_->GetDirectXCommon()->GetCommandList();
        size_t bufferIndex = renderer_->GetAvailableConstantBuffer();

        Vector3 position = { screenPos.x, screenPos.y, 0.0f };
        Vector3 scale    = { layout_.size.x, layout_.size.y, 1.0f };
        Vector3 rotation = { 0.0f, 0.0f, layout_.rotation };

        auto& transformData = renderer_->GetTransformDataPool()[bufferIndex];
        transformData->WVP   = renderer_->CalculateWVPMatrix(position, scale, rotation);
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

} // namespace CoreEngine

