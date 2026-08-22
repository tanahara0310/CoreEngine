#include "pch.h"
#include "PrimitiveGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"

namespace CoreEngine
{
    void PrimitiveGameObject::Initialize()
    {
        // フックで得たメッシュジェネレーターをコンポーネントへ流し込む
        if (auto generator = CreateMeshGenerator()) {
            meshRenderer_->SetPrimitive(std::move(generator));
        }

        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            meshRenderer_->SetTexture(texPath);
        }

        OnInitialize();

        meshRenderer_->ReloadFromSpec();

        SetActive(true);
    }

    const char* PrimitiveGameObject::GetObjectName() const
    {
        return "Primitive";
    }

    std::string PrimitiveGameObject::GetModelPath() const
    {
        return "";
    }
}
