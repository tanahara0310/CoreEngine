#include "pch.h"
#include "CameraShake.h"

#include "Camera/Shake/CameraShaker.h"

namespace CoreEngine
{
    ShakeHandle CameraShake::Play(const CameraShakeParams& params)
    {
        return activeShaker_ ? activeShaker_->Play(params) : 0;
    }

    ShakeHandle CameraShake::Play(const CameraShakeParams& params, const Vector3& worldOrigin)
    {
        return activeShaker_ ? activeShaker_->Play(params, worldOrigin) : 0;
    }

    ShakeHandle CameraShake::PlayPreset(const std::string& name, float scale)
    {
        const CameraShakeParams* params = presetLibrary_.Find(name);
        if (!params || !activeShaker_) {
            return 0;
        }

        if (scale == 1.0f) {
            return activeShaker_->Play(*params);
        }

        // 倍率は振幅にだけ掛ける。周波数や継続時間まで変えると別の揺れになる。
        CameraShakeParams scaled = *params;
        scaled.positionAmplitude = scaled.positionAmplitude * scale;
        scaled.rotationAmplitude = scaled.rotationAmplitude * scale;
        scaled.fovAmplitude *= scale;
        return activeShaker_->Play(scaled);
    }

    void CameraShake::AddTrauma(float amount)
    {
        if (activeShaker_) {
            activeShaker_->AddTrauma(amount);
        }
    }

    void CameraShake::Stop(ShakeHandle handle, float fadeOutSeconds)
    {
        if (activeShaker_) {
            activeShaker_->Stop(handle, fadeOutSeconds);
        }
    }

    void CameraShake::StopAll(float fadeOutSeconds)
    {
        if (activeShaker_) {
            activeShaker_->StopAll(fadeOutSeconds);
        }
    }

    void CameraShake::SetGlobalScale(float scale)
    {
        globalScale_ = (scale > 0.0f) ? scale : 0.0f;
        if (activeShaker_) {
            activeShaker_->SetGlobalScale(globalScale_);
        }
    }

    void CameraShake::SetActiveShaker(CameraShaker* shaker)
    {
        activeShaker_ = shaker;

        // 全体強度はシーンをまたいで保つ。Shaker はシーンごとに作り直されるので、
        // 差し替えのたびにこちらの値を押し込む
        if (activeShaker_) {
            activeShaker_->SetGlobalScale(globalScale_);
        }
    }
}
