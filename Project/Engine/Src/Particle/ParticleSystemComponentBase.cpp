#include "pch.h"
#include "Particle/ParticleSystemComponentBase.h"

#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "Particle/ParticlePresetManager.h"

namespace
{
    /// @brief テクスチャを指していないときに貼るテクスチャ
    constexpr const char* kDefaultTexture = "circle.png";
}

namespace CoreEngine
{
    ParticleSystemComponentBase::ParticleSystemComponentBase()
        : mainModule_(std::make_unique<MainModule>())
        , emissionModule_(std::make_unique<EmissionModule>())
        , shapeModule_(std::make_unique<ShapeModule>())
        , velocityModule_(std::make_unique<VelocityModule>())
        , colorModule_(std::make_unique<ColorModule>())
        , forceModule_(std::make_unique<ForceModule>())
        , sizeModule_(std::make_unique<SizeModule>())
        , rotationModule_(std::make_unique<RotationModule>())
        , noiseModule_(std::make_unique<NoiseModule>())
        , presetManager_(std::make_unique<ParticlePresetManager>())
    {
    }

    ParticleSystemComponentBase::~ParticleSystemComponentBase() = default;

    bool ParticleSystemComponentBase::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<ITransformSource>(other);
    }

    GameObject* ParticleSystemComponentBase::EnsureEmitterTransform()
    {
        GameObject* const owner = GetOwner();
        if (!owner) { return nullptr; }

        // 放出位置は兄弟のトランスフォームから取る。無ければ足す
        if (!owner->GetComponent<ITransformSource>()) {
            owner->AddComponent<TransformComponent>();
        }
        return owner;
    }

    void ParticleSystemComponentBase::FinishAwake()
    {
        awoken_ = true;
        LoadTexture();
    }

    void ParticleSystemComponentBase::Start()
    {
        if (mainModule_->GetData().playOnAwake) {
            Play();
        }
    }

    bool ParticleSystemComponentBase::LoadPreset(const std::string& filePath)
    {
        return presetManager_->LoadPreset(this, filePath);
    }

    bool ParticleSystemComponentBase::SavePreset(const std::string& filePath)
    {
        return presetManager_->SavePreset(this, filePath);
    }

    void ParticleSystemComponentBase::SetTexture(const std::string& texturePath)
    {
        textureAsset_.SetPath(texturePath);
        LoadTexture();
    }

    void ParticleSystemComponentBase::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) { return; }
        textureAsset_.SetValue(value);
        LoadTexture();
    }

    void ParticleSystemComponentBase::LoadTexture()
    {
        // Awake より前は指す先だけを控え、Awake で読み込む
        if (!awoken_) { return; }

        texture_ = TextureManager::GetInstance().Load(
            textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string(kDefaultTexture));
    }

    std::string ParticleSystemComponentBase::DescribeDefaultTexture(const void*)
    {
        return std::string(kDefaultTexture) + "（既定）";
    }

    Vector3 ParticleSystemComponentBase::GetEmitterPosition() const
    {
        const GameObject* const owner = GetOwner();
        return owner ? owner->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
    }

    void ParticleSystemComponentBase::SaveModulesToJson(json& parameters) const
    {
        json settings = ParticlePresetManager::ToJson(const_cast<ParticleSystemComponentBase&>(*this));
        settings.erase("billboardType");
        settings.erase("blendMode");
        parameters.update(settings);
    }

    void ParticleSystemComponentBase::LoadModulesFromJson(const json& parameters)
    {
        ParticlePresetManager::FromJson(*this, parameters);
    }
}
