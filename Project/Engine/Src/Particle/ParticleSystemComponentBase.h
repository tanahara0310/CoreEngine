#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"
#include "Particle/Core/EmitterPlayback.h"
#include "Particle/IParticleSystem.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/NoiseModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/SizeModule.h"
#include "Particle/Modules/VelocityModule.h"

#include <memory>
#include <string>

namespace CoreEngine
{
    class ParticlePresetManager;

    /// @brief CPU 版と GPU 版のパーティクルのコンポーネントの土台
    /// @details 9 つのモジュール・再生の時間軸・プリセット・テクスチャ・ビルボードとブレンドを持ち、放出位置は兄弟のトランスフォームから取る。
    class ParticleSystemComponentBase : public IComponent, public IRenderableComponent, public IParticleSystem
    {
    public:
        ~ParticleSystemComponentBase() override;

        /// @brief 放出位置を取るトランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief 「起動時に再生」なら再生を始める
        void Start() override;

        /// @brief モジュールの値をプリセットと同じ形で書き出す（ビルボードとブレンドは記述子が保存する）
        void SaveModulesToJson(json& parameters) const;

        /// @brief モジュールの値を読む
        void LoadModulesFromJson(const json& parameters);

        // ===== ブレンド =====

        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }

        // ===== 再生 =====

        /// @brief 最初から再生する（経過時間とバーストを戻す）
        void Play() override { playback_.Play(); }

        /// @brief 放出だけを止める（生きている粒は寿命まで更新する）
        void Stop() override { playback_.Stop(); }

        bool IsPlaying() const override { return playback_.IsPlaying(); }
        const EmitterPlayback& GetPlayback() const override { return playback_; }

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

        // ===== 見た目 =====

        /// @brief 板ポリの向き
        void SetBillboardType(BillboardType type) override { billboardType_ = type; }
        BillboardType GetBillboardType() const override { return billboardType_; }

        /// @brief 放出位置（兄弟のトランスフォームのワールド位置）
        Vector3 GetEmitterPosition() const override;

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

    protected:
        /// @brief 9 つのモジュールとプリセットの管理を作る
        ParticleSystemComponentBase();

        /// @brief 放出位置を取るトランスフォームが無ければ足す
        /// @return 持ち主（付いていなければ nullptr）
        GameObject* EnsureEmitterTransform();

        /// @brief 用意が済んだ印を付け、指しているテクスチャ（無ければ既定）を読み込む
        void FinishAwake();

        /// @brief `FinishAwake()` を呼んだか
        bool IsAwoken() const { return awoken_; }

        /// @brief テクスチャを指していないときにテクスチャの欄へ出す文字
        static std::string DescribeDefaultTexture(const void* instance);

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

        BillboardType billboardType_ = BillboardType::ViewFacing;
        EmitterPlayback playback_;

    private:
        /// @brief 指しているテクスチャ（無ければ既定）を読み込む（`FinishAwake()` の前は何もしない）
        void LoadTexture();

        std::unique_ptr<ParticlePresetManager> presetManager_;

        BlendMode blendMode_ = BlendMode::kBlendModeAdd;

        AssetRef<TextureAsset> textureAsset_;
        TextureManager::LoadedTexture texture_{};

        bool awoken_ = false;
    };
}
