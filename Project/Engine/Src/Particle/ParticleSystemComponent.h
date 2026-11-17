#pragma once

#include "Graphics/Asset/AssetRef.h"
#include "Graphics/RayTracing/RTObjectSettings.h"
#include "Math/MathCore.h"
#include "Particle/Core/Particle.h"
#include "Particle/Modules/CollisionModule.h"
#include "Particle/ParticleSystemComponentBase.h"
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
    class ParticleRenderDataBuilder;
    class ParticleResourceManager;
    class ParticleUpdater;

    /// @brief CPU で粒を更新して描くパーティクルのコンポーネント（大量の粒は GpuParticleSystemComponent）
    /// @details 放出位置は兄弟のトランスフォームのワールド位置（無ければ Awake でトランスフォームを足す）。
    ///          モジュールの値はプリセットと同じ形で保存する。
    ///          モデルを指すと、板ポリの代わりにそのモデルを粒として描く。
    class ParticleSystemComponent : public ParticleSystemComponentBase
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

        /// @brief トランスフォームを確保し、インスタンスバッファ・レンダラー・テクスチャ・モデルを用意する
        void Awake() override;

        /// @brief 放出と粒の更新
        void Update() override;

        // ===== IRenderableComponent =====

        /// @brief モデルを指していればモデルの粒のパス、でなければ板ポリの粒のパス
        RenderPassType GetRenderPassType() const override;

        /// @brief 描画データを組み、今のパスのレンダラーで描く
        void Render(const DrawViewInfo& view) override;

        // ===== 再生 =====

        /// @brief 生きている粒を全部消す
        void Clear();

        /// @brief 放出した数を統計へ足しながら粒を放出する
        void Emit(uint32_t count);

        /// @brief 放出が止まり、粒が全部消えたか
        bool IsFinished() const;

        // ===== モデル =====

        /// @brief 粒として描くモデルをパスかファイル名で指す（空なら板ポリに戻す）
        void SetModel(const std::string& modelPath);

        Reflection::AssetRefValue GetModelAsset() const { return modelAsset_.GetValue(); }
        void SetModelAsset(const Reflection::AssetRefValue& value);

        /// @brief 粒として描くモデル（板ポリなら nullptr）
        ModelResource* GetModelResource() const;

        /// @brief モデルを粒として描くか
        bool IsModelParticle() const { return GetModelResource() != nullptr; }

        // ===== レイトレーシング（モデルの粒だけ） =====

        /// @brief レイトレーシングの設定（TLAS に載せるか・影を落とすか・反射と屈折に映るか）
        const RTObjectSettings& GetRayTracingSettings() const { return rayTracing_; }
        bool IsRayTracingEnabled() const { return rayTracing_.enabled; }
        void SetRayTracingEnabled(bool enabled) { rayTracing_.enabled = enabled; }
        RTVisibility GetCastShadows() const { return rayTracing_.castShadows; }
        void SetCastShadows(RTVisibility value) { rayTracing_.castShadows = value; }
        RTVisibility GetVisibleInReflections() const { return rayTracing_.reflected; }
        void SetVisibleInReflections(RTVisibility value) { rayTracing_.reflected = value; }

        // ===== レンダラーが読む値 =====

        uint32_t GetInstanceCount() const { return instanceCount_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingSrvHandleGPU() const;

        /// @brief 生存中の粒のワールド行列を書き出す（モデルの粒だけ。レイトレーシングに使う）
        /// @return 書き出した数
        uint32_t CollectWorldMatrices(Matrix4x4* outMatrices, uint32_t maxCount) const;

        // ===== モジュール =====

        CollisionModule* GetCollisionModule() override { return collisionModule_.get(); }

        // ===== 統計 =====

        uint32_t GetParticleCount() const { return static_cast<uint32_t>(particles_.size()); }

        /// @brief 生存数の上限（メインモジュールの最大数とバッファ容量の小さい方）
        uint32_t GetMaxParticleCount() const;

        const Statistics& GetStatistics() const { return statistics_; }
        void ResetStatistics() { statistics_ = Statistics{}; }

    private:
        /// @brief 指しているモデルを読み込む（無ければ外す）
        void LoadModel();

        std::unique_ptr<CollisionModule> collisionModule_;

        std::unique_ptr<ParticleEmitter> particleEmitter_;
        std::unique_ptr<ParticleUpdater> particleUpdater_;
        std::unique_ptr<ParticleRenderDataBuilder> renderDataBuilder_;
        std::unique_ptr<ParticleResourceManager> resourceManager_;

        /// 板ポリの粒とモデルの粒のレンダラー（Awake で引く）
        BaseParticleRenderer* billboardRenderer_ = nullptr;
        BaseParticleRenderer* modelRenderer_ = nullptr;

        std::vector<Particle> particles_;
        uint32_t instanceCount_ = 0;

        RTObjectSettings rayTracing_;

        AssetRef<ModelAsset> modelAsset_;
        std::unique_ptr<Model> model_;

        Statistics statistics_;
    };
}
