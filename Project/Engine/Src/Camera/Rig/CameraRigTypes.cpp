#include "pch.h"
#include "CameraRigTypes.h"

#include <algorithm>

namespace CoreEngine
{
    namespace
    {
        /// @brief 対象参照の重みを非負へ丸める
        void SanitizeTarget(CameraRigTargetRef& target)
        {
            // 負の重みは中心を対象の外側へ飛ばす。0 は「数に入れない」として通す。
            target.weight = (std::max)(target.weight, 0.0f);
        }
    }

    void CameraRigAsset::Sanitize()
    {
        // ===== Body =====
        SanitizeTarget(body.target);
        for (auto& target : body.targets) {
            SanitizeTarget(target);
        }

        body.frameBias.x = std::clamp(body.frameBias.x, 0.0f, 1.0f);
        body.frameBias.y = std::clamp(body.frameBias.y, 0.0f, 1.0f);
        body.frameBias.z = std::clamp(body.frameBias.z, 0.0f, 1.0f);
        body.framePullBackPerMeter = (std::max)(body.framePullBackPerMeter, 0.0f);
        // 負の上限は引きを前へ反転させる。0 は「上限なし」として通す。
        body.framePullBackMax = (std::max)(body.framePullBackMax, 0.0f);
        body.orbitDistance = (std::max)(body.orbitDistance, 0.0f);
        body.railPosition = std::clamp(body.railPosition, 0.0f, 1.0f);

        // ===== Aim =====
        SanitizeTarget(aim.target);
        for (auto& target : aim.targets) {
            SanitizeTarget(target);
        }

        // 画面内の位置は端まで許す。端に置く構図は実際に使う。
        aim.screenX = std::clamp(aim.screenX, 0.0f, 1.0f);
        aim.screenY = std::clamp(aim.screenY, 0.0f, 1.0f);

        // ===== Lens =====
        lens.fovDegrees = std::clamp(lens.fovDegrees, kMinFovDegrees, kMaxFovDegrees);
        lens.fovMinDegrees = std::clamp(lens.fovMinDegrees, kMinFovDegrees, kMaxFovDegrees);
        lens.fovMaxDegrees = std::clamp(lens.fovMaxDegrees, kMinFovDegrees, kMaxFovDegrees);

        // 入力範囲が潰れていると距離→視野角がゼロ除算になる。わずかに開けておく。
        if (lens.inputMax <= lens.inputMin) {
            lens.inputMax = lens.inputMin + 0.01f;
        }

        // ===== Follow =====
        // 1 を超えると対象の動きを増幅して画が飛ぶ。負は逆向きに動く。どちらも構図にならない。
        follow.weight.x = std::clamp(follow.weight.x, 0.0f, 1.0f);
        follow.weight.y = std::clamp(follow.weight.y, 0.0f, 1.0f);
        follow.weight.z = std::clamp(follow.weight.z, 0.0f, 1.0f);

        // ===== Damping =====
        // 負の減衰は指数が発散する。0 は「減衰なし」として通す。
        damping.position = (std::max)(damping.position, 0.0f);
        damping.rotation = (std::max)(damping.rotation, 0.0f);
        damping.fov = (std::max)(damping.fov, 0.0f);
        damping.aim = (std::max)(damping.aim, 0.0f);
    }
}
