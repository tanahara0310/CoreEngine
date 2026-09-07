#pragma once

#include "Camera/Shake/CameraShakeTypes.h"

/// @file
/// @brief カメラシェイクの EventBus 版の入口

namespace CoreEngine
{
    /// @brief 「この揺れを出してほしい」というイベント
    ///
    /// @details
    /// 静的ファサード（CameraShake）と並ぶもう 1 つの入口。発行側は誰が揺らすかを知らず、
    /// 受け手（CameraShakeFeature）が居なければ何も起きない。
    /// シーンによって揺れを無効化したり、別の受け手（コントローラ振動など）を足したい
    /// ときは、こちらを使っておくと発行側を触らずに済む。
    ///
    /// @code
    ///     EventBus::GetInstance().Publish(
    ///         CameraShakeEvent::At(CameraShakePresets::Explosion(), explosionWorldPosition));
    /// @endcode
    struct CameraShakeEvent {
        CameraShakeParams params{};

        /// @brief 発生源のワールド座標（hasWorldOrigin が true のときだけ意味を持つ）
        Vector3 worldOrigin = { 0.0f, 0.0f, 0.0f };
        bool hasWorldOrigin = false;

        /// @brief 発生源なしの揺れを作る
        static CameraShakeEvent Make(const CameraShakeParams& params)
        {
            CameraShakeEvent event;
            event.params = params;
            return event;
        }

        /// @brief 発生源つきの揺れを作る（爆発・着弾など）
        static CameraShakeEvent At(const CameraShakeParams& params, const Vector3& worldOrigin)
        {
            CameraShakeEvent event;
            event.params = params;
            event.worldOrigin = worldOrigin;
            event.hasWorldOrigin = true;
            return event;
        }
    };

    /// @brief trauma を蓄積するイベント
    /// @details 「小さいダメージを受けた」という事実だけを投げ、揺れの強さの管理は受け手に任せる。
    struct CameraTraumaEvent {
        float amount = 0.0f;
    };

    /// @brief すべての揺れを止めるイベント（シーン遷移・カットシーン開始など）
    struct CameraShakeStopEvent {
        float fadeOutSeconds = 0.0f;
    };
}
