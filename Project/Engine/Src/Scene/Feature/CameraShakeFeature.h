#pragma once

#include "ISceneFeature.h"

#include "Camera/Shake/CameraShaker.h"
#include "Utility/Event/EventBus.h"

/// @file
/// @brief カメラシェイクをゲーム視点カメラへ反映する Feature

namespace CoreEngine
{
    /// @brief 揺れを計算し、ゲーム視点カメラの姿勢へ反映する
    ///
    /// @details
    /// **SceneUpdatePhase::PostLogic + kLateFeaturePriority で回すこと。**
    /// 追従カメラ（OnLateUpdate で構図を決めるコンポーネント）より後でなければ、
    /// 揺れが追従で上書きされて何も起きない。
    /// 同じ PostLogic の他の Feature（大気・雲）はこれより先に回るので、
    /// それらは「揺れる前のカメラ姿勢」を見る。LUT が毎フレームちらつかない。
    ///
    /// 反映は **Camera の基準姿勢を書き換えない**やり方で行う。Update() の中だけで
    /// 「退避 → 揺れを乗せる → 行列を確定 → 即座に戻す」を完結させるので、
    /// 他のコードが揺れた姿勢を観測することはなく、追従ロジックが揺れを積分して
    /// 発散することもない。詳細は .cpp の ApplyShake() を参照。
    ///
    /// 揺らす対象は常にゲーム視点カメラ（CameraManager::GetGameCameraName()）で、
    /// エディタ視点カメラは対象外。デバッグ中に画面が揺れると原因の切り分けができなくなる。
    class CameraShakeFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "CameraShake"; }

        /// @brief 静的ファサードの委譲先になり、EventBus の購読を張る
        void Initialize(SceneContext& ctx) override;

        /// @brief PostLogic で揺れを進め、ゲーム視点カメラへ反映する
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 購読を畳み、委譲先から外れる
        void PostSceneFinalize(SceneContext& ctx) override;

        /// @brief ランタイムへの直接アクセス（デバッグ UI・シーン固有の細かい制御用）
        CameraShaker& GetShaker() { return shaker_; }
        const CameraShaker& GetShaker() const { return shaker_; }

    private:
        CameraShaker shaker_;
        SubscriptionBag subscriptions_;
    };
}
