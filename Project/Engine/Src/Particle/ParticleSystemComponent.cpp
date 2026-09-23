#include "pch.h"
#include "Particle/ParticleSystemComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/Particle/BaseParticleRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Particle/Core/ParticleEmitter.h"
#include "Particle/Core/ParticleRenderDataBuilder.h"
#include "Particle/Core/ParticleResourceManager.h"
#include "Particle/Core/ParticleUpdater.h"
#include "Particle/ParticlePresetManager.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <iterator>

#ifdef _DEBUG
#include "Graphics/Render/Line/LineRendererPipeline.h"
#endif

namespace
{
    /// @brief ブレンドの名前（`BlendMode` の並び）
    constexpr const char* kBlendModeNames[] = { "なし", "アルファ", "加算", "減算", "乗算", "スクリーン" };
    static_assert(std::size(kBlendModeNames) == CoreEngine::kBlendModeCount);

    /// @brief ビルボードの名前（`BillboardType` の並び）
    constexpr const char* kBillboardNames[] = { "なし（粒の回転だけ）", "カメラの正面", "Y 軸だけ回す", "画面と平行" };

    /// @brief テクスチャを指していないときに貼るテクスチャ
    constexpr const char* kDefaultTexture = "circle.png";

    /// @brief テクスチャを指していないときにテクスチャの欄へ出す文字
    std::string DescribeDefaultTexture(const void*)
    {
        return std::string(kDefaultTexture) + "（既定）";
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::ParticleSystemComponent, "パーティクル")
    REFLECT_PARTIAL()
    REFLECT_JSON(SaveModulesToJson, LoadModulesFromJson)
    REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
        p.assetType = ::CoreEngine::AssetType::Texture, p.emptyText = &DescribeDefaultTexture)
    REFLECT_ACCESSOR("model", "モデル", GetModelAsset, SetModelAsset,
        p.assetType = ::CoreEngine::AssetType::Model,
        p.tooltip = "指すと、板ポリの代わりにこのモデルを粒として描く")
    REFLECT_ENUM_ACCESSOR("blendMode", "ブレンド", GetBlendMode, SetBlendMode, kBlendModeNames,
        p.tooltip = "背景との合成のしかた。炎や光は「加算」、煙は「アルファ」が向く")
    REFLECT_ENUM_ACCESSOR("billboard", "ビルボード", GetBillboardType, SetBillboardType, kBillboardNames,
        p.tooltip = "板ポリの向き。草や炎の柱のように立てたいものは「Y 軸だけ回す」。モデルの粒では使わない")
    REFLECT_METHOD("Play", "再生", Play)
    REFLECT_METHOD("Stop", "停止", Stop)
    REFLECT_METHOD("IsPlaying", "再生中か", IsPlaying)
    REFLECT_METHOD("Clear", "粒を消す", Clear)
    REFLECT_METHOD("Emit", "放出", Emit, m.tooltip = "指定した数の粒をすぐに出す")
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::ParticleSystemComponent)
COMPONENT_REGISTER(CoreEngine::ParticleSystemComponent)

namespace CoreEngine
{
    ParticleSystemComponent::ParticleSystemComponent()
        : mainModule_(std::make_unique<MainModule>())
        , emissionModule_(std::make_unique<EmissionModule>())
        , shapeModule_(std::make_unique<ShapeModule>())
        , velocityModule_(std::make_unique<VelocityModule>())
        , colorModule_(std::make_unique<ColorModule>())
        , forceModule_(std::make_unique<ForceModule>())
        , sizeModule_(std::make_unique<SizeModule>())
        , rotationModule_(std::make_unique<RotationModule>())
        , noiseModule_(std::make_unique<NoiseModule>())
        , collisionModule_(std::make_unique<CollisionModule>())
        , particleEmitter_(std::make_unique<ParticleEmitter>())
        , particleUpdater_(std::make_unique<ParticleUpdater>())
        , renderDataBuilder_(std::make_unique<ParticleRenderDataBuilder>())
        , presetManager_(std::make_unique<ParticlePresetManager>())
    {
        // インスペクタの最大数の欄をバッファの容量までにする
        mainModule_->SetCapacityLimit(kNumMaxInstance);

        particleEmitter_->Initialize(mainModule_.get(), emissionModule_.get(), shapeModule_.get(),
            velocityModule_.get(), rotationModule_.get());
        particleUpdater_->Initialize(forceModule_.get(), colorModule_.get(), sizeModule_.get(),
            rotationModule_.get(), noiseModule_.get(), collisionModule_.get());
    }

    ParticleSystemComponent::~ParticleSystemComponent() = default;

    void ParticleSystemComponent::Awake()
    {
        GameObject* const owner = GetOwner();
        if (!owner) { return; }

        // 放出位置は兄弟のトランスフォームから取る。無ければ足す
        if (!owner->GetComponent<ITransformSource>()) {
            owner->AddComponent<TransformComponent>();
        }

        EngineSystem* const engine = owner->GetEngineSystem();
        GraphicsCore* const graphics = engine ? engine->GetService<GraphicsCore>() : nullptr;
        RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
        if (!graphics || !renderManager) { return; }

        // 最大数ぶんを先に確保して、粒が増えるたびの確保をなくす
        particles_.reserve(kNumMaxInstance);

        resourceManager_ = std::make_unique<ParticleResourceManager>();
        resourceManager_->Initialize(graphics, kNumMaxInstance);

        billboardRenderer_ = dynamic_cast<BaseParticleRenderer*>(renderManager->GetRenderer(RenderPassType::Particle));
        modelRenderer_ = dynamic_cast<BaseParticleRenderer*>(renderManager->GetRenderer(RenderPassType::ModelParticle));

        awoken_ = true;
        LoadTexture();
        LoadModel();
    }

    void ParticleSystemComponent::Start()
    {
        if (mainModule_->GetMainData().playOnAwake) {
            Play();
        }
    }

    void ParticleSystemComponent::Update()
    {
        const float deltaTime = Time::DeltaTime();

        // メインモジュールが無効ならシステム全体を止める
        if (!mainModule_->IsEnabled()) {
            return;
        }

        statistics_.systemRuntime += deltaTime;

        mainModule_->UpdateTime(deltaTime);
        const float elapsedTime = mainModule_->GetElapsedTime();
        const auto& mainData = mainModule_->GetMainData();

        // バーストのための放出モジュールの時間
        emissionModule_->UpdateTime(deltaTime);

        // ループで経過時間が巻き戻ったら、放出もやり直す
        if (mainModule_->IsPlaying() && mainData.looping && elapsedTime < lastElapsedTime_) {
            emissionModule_->Play();
        }
        lastElapsedTime_ = elapsedTime;

        bool shouldEmit = emissionModule_->IsPlaying() && emissionModule_->IsEnabled();
        if (!mainData.looping && elapsedTime >= mainData.duration) {
            // 長さの後に置いたバーストは、長さに達したときに出してから止める
            const auto& emissionData = emissionModule_->GetEmissionData();
            if (emissionData.burstCount > 0 && emissionData.burstTime >= mainData.duration) {
                Emit(emissionModule_->CalculateEmissionCount(deltaTime));
            }
            shouldEmit = false;
            emissionModule_->Stop();
        }

        if (shouldEmit) {
            Emit(emissionModule_->CalculateEmissionCount(deltaTime));
        }

        const uint32_t destroyed = particleUpdater_->UpdateParticles(particles_, deltaTime, mainData.gravityModifier);
        statistics_.totalParticlesDestroyed += destroyed;
        statistics_.peakParticleCount = (std::max)(statistics_.peakParticleCount, GetParticleCount());
    }

    void ParticleSystemComponent::Emit(uint32_t count)
    {
        if (count == 0) { return; }
        statistics_.totalParticlesCreated +=
            particleEmitter_->EmitParticles(count, GetEmitterPosition(), GetMaxParticleCount(), particles_);
    }

    RenderPassType ParticleSystemComponent::GetRenderPassType() const
    {
        return IsModelParticle() ? RenderPassType::ModelParticle : RenderPassType::Particle;
    }

    void ParticleSystemComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        const Camera* const camera = view.GetCamera();
        if (!owner || !owner->IsActive() || !camera || !resourceManager_) { return; }

        // モデルの粒は、影（CollectWorldMatrices）と同じくビルボードを使わない
        const bool modelParticle = IsModelParticle();
        instanceCount_ = renderDataBuilder_->BuildRenderData(
            particles_,
            camera,
            modelParticle ? BillboardType::None : billboardType_,
            modelParticle ? ParticleRenderMode::Model : ParticleRenderMode::Billboard,
            resourceManager_->GetInstancingData(),
            kNumMaxInstance);

#ifdef _DEBUG
        // 放出形状の線を足す
        if (shapeModule_->IsDebugDrawEnabled()) {
            EngineSystem* const engine = owner->GetEngineSystem();
            RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
            auto* const pipeline = renderManager
                ? static_cast<LineRendererPipeline*>(renderManager->GetRenderer(RenderPassType::Line)) : nullptr;
            if (pipeline) {
                shapeModule_->DrawEmitterShape(pipeline, camera, GetEmitterPosition());
            }
        }
#endif

        // パスの開始はキューを流す側が済ませているので、今のパスのレンダラーで描くだけ
        if (BaseParticleRenderer* const renderer = modelParticle ? modelRenderer_ : billboardRenderer_) {
            renderer->Draw(this);
        }
    }

    void ParticleSystemComponent::Play()
    {
        mainModule_->Play();
        emissionModule_->Play();
    }

    void ParticleSystemComponent::Stop()
    {
        mainModule_->Stop();
        emissionModule_->Stop();
    }

    bool ParticleSystemComponent::IsPlaying() const
    {
        return mainModule_->IsPlaying() && emissionModule_->IsPlaying();
    }

    void ParticleSystemComponent::Clear()
    {
        particles_.clear();
        instanceCount_ = 0;
    }

    bool ParticleSystemComponent::IsFinished() const
    {
        return !emissionModule_->IsPlaying() && particles_.empty();
    }

    bool ParticleSystemComponent::LoadPreset(const std::string& filePath)
    {
        return presetManager_->LoadPreset(this, filePath);
    }

    bool ParticleSystemComponent::SavePreset(const std::string& filePath)
    {
        return presetManager_->SavePreset(this, filePath);
    }

    void ParticleSystemComponent::SetTexture(const std::string& texturePath)
    {
        textureAsset_.SetPath(texturePath);
        LoadTexture();
    }

    void ParticleSystemComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) { return; }
        textureAsset_.SetValue(value);
        LoadTexture();
    }

    void ParticleSystemComponent::LoadTexture()
    {
        // Awake より前は指す先だけを控え、Awake で読み込む
        if (!awoken_) { return; }

        texture_ = TextureManager::GetInstance().Load(
            textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string(kDefaultTexture));
    }

    void ParticleSystemComponent::SetModel(const std::string& modelPath)
    {
        modelAsset_.SetPath(modelPath);
        LoadModel();
    }

    void ParticleSystemComponent::SetModelAsset(const Reflection::AssetRefValue& value)
    {
        if (value == modelAsset_.GetValue()) { return; }
        modelAsset_.SetValue(value);
        LoadModel();
    }

    void ParticleSystemComponent::LoadModel()
    {
        if (!awoken_) { return; }

        model_.reset();
        if (modelAsset_.IsSet()) {
            const GameObject* const owner = GetOwner();
            EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
            if (ModelManager* const models = engine ? engine->GetService<ModelManager>() : nullptr) {
                model_ = models->CreateStaticModel(modelAsset_.GetPath());
            }
        }
    }

    ModelResource* ParticleSystemComponent::GetModelResource() const
    {
        return model_ ? model_->GetModelResource() : nullptr;
    }

    Vector3 ParticleSystemComponent::GetEmitterPosition() const
    {
        const GameObject* const owner = GetOwner();
        return owner ? owner->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ParticleSystemComponent::GetInstancingSrvHandleGPU() const
    {
        return resourceManager_ ? resourceManager_->GetSrvHandleGPU() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    uint32_t ParticleSystemComponent::GetMaxParticleCount() const
    {
        return (std::min)(mainModule_->GetMainData().maxParticles, kNumMaxInstance);
    }

    uint32_t ParticleSystemComponent::CollectWorldMatrices(Matrix4x4* outMatrices, uint32_t maxCount) const
    {
        if (!outMatrices || maxCount == 0 || !IsModelParticle()) {
            return 0;
        }

        // 描画側（ParticleRenderDataBuilder::BuildRenderData）と同じ打ち切り方をする。
        // ここがずれると、影だけ出る粒／影だけ消える粒が生まれる。
        const uint32_t limit = (std::min)(maxCount, kNumMaxInstance);

        uint32_t count = 0;
        for (const auto& particle : particles_) {
            if (count >= limit) {
                break;
            }
            outMatrices[count] = ParticleRenderDataBuilder::MakeModelParticleWorldMatrix(particle);
            ++count;
        }
        return count;
    }

    bool ParticleSystemComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const ITransformSource*>(&other) != nullptr;
    }

    void ParticleSystemComponent::SaveModulesToJson(json& parameters) const
    {
        json settings = ParticlePresetManager::ToJson(const_cast<ParticleSystemComponent&>(*this));
        settings.erase("billboardType");
        settings.erase("blendMode");
        parameters.update(settings);
    }

    void ParticleSystemComponent::LoadModulesFromJson(const json& parameters)
    {
        ParticlePresetManager::FromJson(*this, parameters);
    }
}
