#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"
#include "Particle/Core/Particle.h"
#include "Particle/IParticleSystem.h"
#include "Particle/Modules/CollisionModule.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/NoiseModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/SizeModule.h"
#include "Particle/Modules/VelocityModule.h"
#include "Reflection/Reflect.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
    class BaseParticleRenderer;
    class Model;
    class ModelResource;
    class ParticleEmitter;
    class ParticlePresetManager;
    class ParticleRenderDataBuilder;
    class ParticleResourceManager;
    class ParticleUpdater;

    /// @brief CPU で粒を更新して描くパーティクルのコンポーネント（大量の粒は GpuParticleSystemComponent）
    /// @details 放出位置は兄弟のトランスフォームのワールド位置（無ければ Awake でトランスフォームを足す）。
    ///          モジュールの値はプリセットと同じ形で保存する。
    ///          モデルを指すと、板ポリの代わりにそのモデルを粒として描く。
    class ParticleSystemComponent : public IComponent, public IRenderableComponent, public IParticleSystem
    {
    public:
        /// @brief インスタンスバッファの容量（生存数の上限はメインモジュールの最大数でさらに絞る）
        static constexpr uint32_t kNumMaxInstance = 1028;

        /// @brief 稼働統計（インスペクタに出す）
        struct Statistics {
            uint32_t totalParticlesCreated = 0;
            uint32_t totalParticlesDestroyed = 0;
            uint32_t peakParticleCount = 0;
            float systemRuntime = 0.0f;
        };

        ParticleSystemComponent();
        ~ParticleSystemComponent() override;

        const char* GetTypeName() const override { return "ParticleSystem"; }

        REFLECT_DECLARE(ParticleSystemComponent)

        /// @brief 放出位置を取るトランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "パーティクル"; }

        /// @brief 再生の操作・プリセット・モジュール・統計を描く（モジュールの編集は Undo に積む）
        bool DrawInspector() override;
#endif

        /// @brief トランスフォームを確保し、インスタンスバッファ・レンダラー・テクスチャ・モデルを用意する
        void Awake() override;

        /// @brief 「起動時に再生」なら再生を始める
        void Start() override;

        /// @brief 放出と粒の更新
        void Update() override;

        /// @brief モジュールの値をプリセットと同じ形で書き出す
        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

        // ===== IRenderableComponent =====

        /// @brief モデルを指していればモデルの粒のパス、でなければ板ポリの粒のパス
        RenderPassType GetRenderPassType() const override;
        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }

        /// @brief 描画データを組み、今のパスのレンダラーで描く
        void Render(const DrawViewInfo& view) override;

        // ===== 再生 =====

        void Play() override;
        void Stop() override;
        bool IsPlaying() const override;

        /// @brief 生きている粒を全部消す
        void Clear();

        /// @brief 放出が止まり、粒が全部消えたか
        bool IsFinished() const;

        // ===== プリセット =====

        /// @brief プリセットを読み込んで各モジュールへ反映する
        bool LoadPreset(const std::string& filePath);

        /// @brief 今の設定をプリセットとして書き出す
        bool SavePreset(const std::string& filePath);

        ParticlePresetManager& GetPresetManager() { return *presetManager_; }

        // ===== テクスチャ =====

        /// @brief テクスチャをパスかファイル名で指す（空なら既定のテクスチャ）
        void SetTexture(const std::string& texturePath) override;

        Reflection::AssetRefValue GetTextureAsset() const { return textureAsset_.GetValue(); }
        void SetTextureAsset(const Reflection::AssetRefValue& value);

        /// @brief 粒に貼るテクスチャ（読み込む前は 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle() const { return texture_.gpuHandle; }

        // ===== モデル =====

        /// @brief 粒として描くモデルをパスかファイル名で指す（空なら板ポリに戻す）
        void SetModel(const std::string& modelPath);

        Reflection::AssetRefValue GetModelAsset() const { return modelAsset_.GetValue(); }
        void SetModelAsset(const Reflection::AssetRefValue& value);

        /// @brief 粒として描くモデル（板ポリなら nullptr）
        ModelResource* GetModelResource() const;

        /// @brief モデルを粒として描くか
        bool IsModelParticle() const { return GetModelResource() != nullptr; }

        // ===== 見た目 =====

        /// @brief 板ポリの向き（モデルの粒では使わない）
        void SetBillboardType(BillboardType type) override { billboardType_ = type; }
        BillboardType GetBillboardType() const override { return billboardType_; }

        /// @brief 放出位置（兄弟のトランスフォームのワールド位置）
        Vector3 GetEmitterPosition() const override;

        // ===== レンダラーが読む値 =====

        uint32_t GetInstanceCount() const { return instanceCount_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingSrvHandleGPU() const;

        /// @brief 生存中の粒のワールド行列を書き出す（モデルの粒だけ。レイトレの影に使う）
        /// @return 書き出した数
        uint32_t CollectWorldMatrices(Matrix4x4* outMatrices, uint32_t maxCount) const;

        // ===== モジュール =====

        MainModule& GetMainModule() override { return *mainModule_; }
        EmissionModule& GetEmissionModule() override { return *emissionModule_; }
        ShapeModule& GetShapeModule() override { return *shapeModule_; }
        VelocityModule& GetVelocityModule() override { return *velocityModule_; }
        ColorModule& GetColorModule() override { return *colorModule_; }
        ForceModule& GetForceModule() override { return *forceModule_; }
        SizeModule& GetSizeModule() override { return *sizeModule_; }
        RotationModule& GetRotationModule() override { return *rotationModule_; }
        NoiseModule& GetNoiseModule() override { return *noiseModule_; }
        CollisionModule* GetCollisionModule() override { return collisionModule_.get(); }

        // ===== 統計 =====

        uint32_t GetParticleCount() const { return static_cast<uint32_t>(particles_.size()); }

        /// @brief 生存数の上限（メインモジュールの最大数とバッファ容量の小さい方）
        uint32_t GetMaxParticleCount() const;

        const Statistics& GetStatistics() const { return statistics_; }
        void ResetStatistics() { statistics_ = Statistics{}; }

    private:
        /// @brief 指しているテクスチャ（無ければ既定）を読み込む
        void LoadTexture();

        /// @brief 指しているモデルを読み込む（無ければ外す）
        void LoadModel();

        /// @brief 放出した数を統計へ足しながら粒を放出する
        void Emit(uint32_t count);

        // モジュール
        std::unique_ptr<MainModule> mainModule_;
        std::unique_ptr<EmissionModule> emissionModule_;
        std::unique_ptr<ShapeModule> shapeModule_;
        std::unique_ptr<VelocityModule> velocityModule_;
        std::unique_ptr<ColorModule> colorModule_;
        std::unique_ptr<ForceModule> forceModule_;
        std::unique_ptr<SizeModule> sizeModule_;
        std::unique_ptr<RotationModule> rotationModule_;
        std::unique_ptr<NoiseModule> noiseModule_;
        std::unique_ptr<CollisionModule> collisionModule_;

        std::unique_ptr<ParticleEmitter> particleEmitter_;
        std::unique_ptr<ParticleUpdater> particleUpdater_;
        std::unique_ptr<ParticleRenderDataBuilder> renderDataBuilder_;
        std::unique_ptr<ParticleResourceManager> resourceManager_;
        std::unique_ptr<ParticlePresetManager> presetManager_;

        /// 板ポリの粒とモデルの粒のレンダラー（Awake で引く）
        BaseParticleRenderer* billboardRenderer_ = nullptr;
        BaseParticleRenderer* modelRenderer_ = nullptr;
        bool awoken_ = false;

        std::vector<Particle> particles_;
        uint32_t instanceCount_ = 0;

        BillboardType billboardType_ = BillboardType::ViewFacing;
        BlendMode blendMode_ = BlendMode::kBlendModeAdd;

        AssetRef<TextureAsset> textureAsset_;
        TextureManager::LoadedTexture texture_{};

        AssetRef<ModelAsset> modelAsset_;
        std::unique_ptr<Model> model_;

        Statistics statistics_;
        /// ループで経過時間が巻き戻ったかを見るための、前の更新の経過時間
        float lastElapsedTime_ = 0.0f;

#ifdef USE_IMGUI
        /// モジュールの編集を始める前の値（編集が終わったら Undo に積む）
        json moduleEditBefore_;
        bool moduleEditActive_ = false;
#endif
    };
}
