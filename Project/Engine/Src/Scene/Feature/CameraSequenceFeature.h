#pragma once

#include "ISceneFeature.h"

#include "Camera/Sequence/CameraSequencePlayer.h"

/// @file
/// @brief カメラシーケンスの再生結果をゲーム視点カメラへ反映する Feature

namespace CoreEngine
{
    /// @brief 再生中のシーケンスの構図を、ゲーム視点カメラへ書き込む
    ///
    /// @details
    /// **SceneUpdatePhase::PostLogic + kLateFeaturePriority で、CameraShakeFeature より
    /// 先に登録すること。** この 2 つの順序がこの Feature の設計そのものになっている。
    ///
    ///  - 追従カメラ（OnLateUpdate で構図を決めるコンポーネント）より後 …
    ///    先に回すとシーケンスの構図が追従に上書きされ、再生しても何も起きない。
    ///    PostLogic は OnLateUpdate の後なので、このフェーズに居るだけで満たされる。
    ///  - CameraShakeFeature より先 …
    ///    揺れはシーケンスが決めた構図の上に乗せたい。逆順だと揺れが毎フレーム
    ///    シーケンスに消される。
    ///
    /// つまり「ゲーム側の追従 → シーケンス → 揺れ」の順で 1 つのカメラを重ね書きする。
    /// 誰がカメラを握っているかは CameraSequence::IsActive() で外から見える。
    ///
    /// 対象は常にゲーム視点カメラ（CameraManager::GetGameCameraName()）で、エディタ視点
    /// カメラは対象外。編集中に画面が奪われると構図の確認ができなくなる。
    ///
    /// 再生していない間は一切カメラに触れないので、ゲーム側の追従がそのまま効く。
    class CameraSequenceFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "CameraSequence"; }

        /// @brief 静的ファサード（CameraSequence::Play など）の委譲先になる
        void Initialize(SceneContext& ctx) override;

        /// @brief PostLogic で再生ヘッドを進め、ゲーム視点カメラへ構図を書き込む
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 委譲先から外れ、再生を畳む
        void PostSceneFinalize(SceneContext& ctx) override;

        /// @brief ランタイムへの直接アクセス（デバッグ UI・シーン固有の細かい制御用）
        CameraSequencePlayer& GetPlayer() { return player_; }
        const CameraSequencePlayer& GetPlayer() const { return player_; }

    private:
        /// @brief 跨いだイベント 1 件を実際の演出へ変換する
        void DispatchEvent(const CameraSequenceEvent& event);

        /// @brief TimeScale イベントの残り時間を進め、切れたら等倍へ戻す
        void UpdateTimeScaleOverride(float unscaledDeltaTime);

        CameraSequencePlayer player_;

        // TimeScale イベントで変更した時間スケールを戻すまでの残り秒数
        float timeScaleRemaining_ = 0.0f;

        // ブレンドイン用。再生が始まった瞬間のカメラ姿勢を繋ぎ元として控える。
        CameraSnapshot blendFrom_{};
        bool hasBlendFrom_ = false;
        std::uint32_t observedPlayId_ = 0;
    };
}
