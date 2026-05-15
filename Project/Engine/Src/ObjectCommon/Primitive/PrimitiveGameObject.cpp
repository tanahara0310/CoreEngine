#include "PrimitiveGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"

namespace CoreEngine
{
    void PrimitiveGameObject::Initialize()
    {
        auto* engine   = GetEngineSystem();
        auto* dxCommon = engine->GetComponent<DirectXCommon>();
        auto* modelMgr = engine->GetComponent<ModelManager>();

        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        auto generator = CreateMeshGenerator();
        if (generator && modelMgr) {
            model_ = modelMgr->CreatePrimitiveModel(generator->GetCacheKey(), *generator);
        }

        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            texture_     = TextureManager::GetInstance().Load(texPath);
            textureName_ = texPath;
        }

        OnInitialize();

        // OnInitialize() で SetCustomShaderProvider() が呼ばれた場合、カスタム PSO を構築する
        BuildCustomShaderPipelineIfNeeded(dxCommon ? dxCommon->GetDevice() : nullptr, modelMgr);

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
