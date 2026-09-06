#include "pch.h"
#include "CameraRigRuntime.h"

#include <algorithm>

namespace CoreEngine
{
    void CameraRigRuntime::Activate(std::shared_ptr<const CameraRigAsset> asset,
        const CameraRigActivateOptions& options, const std::string& name)
    {
        if (!asset) {
            return;
        }

        asset_ = std::move(asset);
        options_ = options;
        name_ = name;

        // 繋ぎ時間が 0 なら最初から重み 1（＝即座に切り替え）。
        blendWeight_ = (options_.blendSeconds > 0.0f) ? 0.0f : 1.0f;

        if (options_.resetState) {
            state_.Reset();
        }

        // 0 は「止まっている」の意味に使うので跨がせる。
        ++activationId_;
        if (activationId_ == 0) {
            activationId_ = 1;
        }
    }

    void CameraRigRuntime::Deactivate()
    {
        asset_.reset();
        name_.clear();
        state_.Reset();
        blendWeight_ = 1.0f;
        activationId_ = 0;
    }

    void CameraRigRuntime::ReplaceAsset(std::shared_ptr<const CameraRigAsset> asset)
    {
        // 止まっている間に入れ替えると、動かしていないリグが動き出したように見える。
        if (!asset_ || !asset) {
            return;
        }
        asset_ = std::move(asset);
    }

    void CameraRigRuntime::Update(float deltaTime, float unscaledDeltaTime)
    {
        if (!asset_ || blendWeight_ >= 1.0f || options_.blendSeconds <= 0.0f) {
            return;
        }

        const float elapsed = options_.useUnscaledTime ? unscaledDeltaTime : deltaTime;
        blendWeight_ = (std::min)(blendWeight_ + elapsed / options_.blendSeconds, 1.0f);
    }

    bool CameraRigRuntime::Evaluate(float deltaTime, const CameraRigContext* context,
        CameraSnapshot& inOutSnapshot)
    {
        if (!asset_) {
            return false;
        }

        return CameraRigEvaluator::Evaluate(*asset_, context, deltaTime, state_, inOutSnapshot);
    }
}
