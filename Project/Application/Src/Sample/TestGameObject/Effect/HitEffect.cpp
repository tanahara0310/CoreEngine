#include "pch.h"
#include "HitEffect.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Particle/ParticleSystem.h"
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/SizeModule.h"

#include <numbers>

namespace CoreEngine
{

    void HitEffect::Initialize(ParticleSystem* particleSystem,
        DirectXCommon* dxCommon,
        ResourceFactory* factory,
        const std::string& texturePath)
    {
        particleSystem_ = particleSystem;
        particleSystem_->Initialize(dxCommon, factory, "HitEffect");
        particleSystem_->SetTexture(texturePath);
        particleSystem_->SetBlendMode(BlendMode::kBlendModeAdd);
        particleSystem_->SetBillboardType(BillboardType::ViewFacing);

        SetupHitEffectParameters();
    }

    void HitEffect::SetupHitEffectParameters()
    {
        // ===== MainModule =====
        // 短命・ワンショット
        auto& mainData = particleSystem_->GetMainModule().GetMainData();
        mainData.duration = 0.8f;
        mainData.looping = false;
        mainData.playOnAwake = false;
        mainData.maxParticles = 20;

        // 寿命
        mainData.startLifetime = 0.6f;
        mainData.startLifetimeRandomness = 0.2f;

        // 速度なし（発生地点から動かさない）
        mainData.startSpeed = 0.0f;
        mainData.startSpeedRandomness = 0.0f;

        // 縦長スケール: X/Z を細く、Y を大きく伸ばす
        mainData.startSize             = { 0.4f, 2.5f, 0.4f };
        mainData.startSizeRandomness   = 0.15f;

        // 回転なし（縦長のまま放射する）
        mainData.startRotation = { 0.0f, 0.0f, 0.0f };
        mainData.startRotationRandomness = 0.0f;

        // 色：鮮やかな赤（分かりやすい発光色）
        mainData.startColor = { 1.0f, 0.2f, 0.05f, 1.0f };

        // ===== EmissionModule =====
        // ワンショットバースト
        auto& emissionData = particleSystem_->GetEmissionModule().GetEmissionData();
        emissionData.rateOverTime = 0;
        emissionData.burstCount = 12;
        emissionData.burstTime = 0.0f;

        // ===== ShapeModule =====
        // 点から全方向に放射（球形）
        ShapeModule::ShapeData shapeData{};
        shapeData.shapeType = ShapeModule::ShapeType::Sphere;
        shapeData.radius    = 0.0f; // 発生地点を一点に集約
        particleSystem_->GetShapeModule().SetShapeData(shapeData);

        // ===== RotationModule =====
        // 生存中にZ軸回転させてパーティクルに動きを付ける
        RotationModule::RotationData rotData{};
        rotData.use2DRotation = true;
        rotData.rotation2DSpeed = static_cast<float>(std::numbers::pi); // 1π rad/s
        rotData.rotation2DSpeedRandomness = 0.8f;
        rotData.rotationDirection = RotationModule::RotationData::RotationDirection::Random;
        particleSystem_->GetRotationModule().SetRotationData(rotData);

        // ===== ColorModule =====
        // ライフタイムで赤→オレンジへフェードアウト
        ColorModule::ColorOverLifetime colorData{};
        colorData.endColor = { 1.0f, 0.6f, 0.0f, 0.0f };
        colorData.useGradient = true;
        particleSystem_->GetColorModule().SetColorData(colorData);

        // ===== SizeModule =====
        // 3Dサイズで縦長を維持しながら縮小
        SizeModule::SizeData sizeData{};
        sizeData.sizeOverLifetime = true;
        sizeData.use3DSize = true;
        sizeData.endSize3D = { 0.0f, 0.0f, 0.0f };
        sizeData.sizeCurve = SizeModule::SizeData::SizeCurve::EaseIn;
        particleSystem_->GetSizeModule().SetSizeData(sizeData);
    }

    void HitEffect::Play(const Vector3& position)
    {
        if (!particleSystem_) {
            return;
        }

        particleSystem_->SetEmitterPosition(position);
        particleSystem_->Clear();
        // 経過時間・バースト状態をリセットしてから再生
        particleSystem_->GetMainModule().Restart();
        particleSystem_->GetEmissionModule().Play();
    }

    bool HitEffect::IsPlaying() const
    {
        if (!particleSystem_) {
            return false;
        }
        return particleSystem_->IsPlaying();
    }

} // namespace CoreEngine
