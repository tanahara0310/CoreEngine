#include "pch.h"
#include "StationSlowdownEffect.h"

#include "Components/Building/MapViewComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "Components/UI/SpeedGaugeUIComponent.h"

#include "Audio/AudioSystem.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Scene/Feature/ISceneFeature.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstdint>
#include <memory>

using namespace CoreEngine;

namespace {

    // ──────────────────────────────────────────────────────────
    // 駅の減速演出
    // ──────────────────────────────────────────────────────────
    // 先頭が未訪問の駅前レールへ入った瞬間、TrainMovementComponent が
    // 速度を最低速度まで落とす。その「直前」に HungerComponent から通知が来るので、
    // ここで 4 つを同時に鳴らして、原因と結果を 1 つの出来事として束ねる。
    //
    //   原因 … 駅チップが跳ねる（この駅がやった、と画面の中で名指しする）
    //   衝撃 … カメラが進行方向へつんのめる（急に速度を失った体）
    //   数値 … 速度計が赤く点滅しながら沈む（何が減ったのか）
    //   音   … 低い連結音（因果を一番安く繋ぐ層）
    //
    // ■ 強さは落差に比例させてある
    //   駅が落とすのは「そこまでに稼いだ加速ぶん」なので、落差は走ってきた距離で変わる。
    //   発車直後の駅はほとんど落ちないし、長く走った後の駅は大きく落ちる。
    //   常に最大で鳴らすと、落ちていないときまで大げさになって嘘をつく。
    //
    // ■ 落差がほぼ無いときは鳴らさない
    //   説明すべき変化が起きていないので、鳴らすと「駅は減速する」という
    //   誤った学習をさせてしまう。駅そのものの反応は連結時の StationPop が別に出す。
    //
    // ■ 連結（サルが増える）の瞬間には手を出していない
    //   そちらは最後尾が駅を抜けてからで、既存の StationPop と車両の出現演出が担当する。
    //   ここで先に「駅が跳ねる」ことで、後から来る連結と同じ駅が結び付く。

    // ===== 有効・無効 =====

    CVar<bool> cvEnabled{
        "Game.StationSlowdown.Enabled", true,
        "駅でトロッコの速度が落ちる瞬間に、減速の演出を出す" };

    // ===== 強さの決め方 =====

    CVar<float> cvFullDropCells{
        "Game.StationSlowdown.FullDropCells", 3.0f,
        "この速度差［マス/秒］ぶん落ちたら演出が最大の強さになる。"
        "駅は加速ぶんを丸ごと最低速度まで落とすので、落差は走ってきた距離で変わる",
        CVarRange{ 0.05f, 10.0f } };

    CVar<float> cvMinDropCells{
        "Game.StationSlowdown.MinDropCells", 0.08f,
        "これ以下の速度差［マス/秒］なら演出を出さない。"
        "説明すべき変化が起きていないのに鳴らすと、駅は必ず減速すると誤解させてしまう",
        CVarRange{ 0.0f, 2.0f } };

    // ===== カメラ =====

    CVar<float> cvShakeScale{
        "Game.StationSlowdown.ShakeScale", 1.0f,
        "つんのめる揺れの強さ。0 で揺らさない",
        CVarRange{ 0.0f, 3.0f } };

    CVar<float> cvShakeSeconds{
        "Game.StationSlowdown.ShakeSeconds", 0.3f,
        "つんのめる揺れの長さ［秒］",
        CVarRange{ 0.05f, 1.5f } };

    // ===== 音 =====

    CVar<bool> cvSeEnabled{
        "Game.StationSlowdown.SeEnabled", true,
        "減速に合わせて連結音を鳴らす" };

    CVar<float> cvSeVolume{
        "Game.StationSlowdown.SeVolume", 0.7f,
        "連結音の音量",
        CVarRange{ 0.0f, 1.5f } };

    CVar<float> cvSePitch{
        "Game.StationSlowdown.SePitch", 0.55f,
        "連結音のピッチ。下げるほど重い金具の音になる",
        CVarRange{ 0.25f, 2.0f } };

    /// レール設置音を低く鳴らして連結音の代わりにしている。専用の SE ができたら差し替える。
    /// 新規アセットを増やさずに済ませているだけで、この音でなければならない理由は無い
    constexpr const char* kCouplingSe = "Application/Assets/Sounds/SE/build.mp3";

    /// @brief 減速でカメラを進行方向へつんのめらせる揺れ
    /// @details 着地（Landing）と同じ 1 発蹴る形だが、向きはカメラローカルの +X（＝進行方向）。
    ///          急に速度を失うと、乗っている側は進行方向へ投げ出される。
    /// @param strength 落差から決めた 0〜1 の強さ
    CameraShakeParams MakeLurchShake(float strength)
    {
        CameraShakeParams params;
        // 位置の揺れは壁へめり込むので cm オーダーに留め、読ませるのは回転側で作る
        params.positionAmplitude = { 0.12f * strength, 0.03f * strength, 0.0f };
        params.rotationAmplitude = { 0.8f * strength, 0.5f * strength, 1.6f * strength };
        params.frequency = 5.0f;   // 0.3s x 5Hz = 1.5 周期。つんのめって戻るだけの形
        params.duration = std::max(cvShakeSeconds.Get(), 0.05f);
        params.decayEase = EasingUtil::Type::EaseOutCubic;
        params.direction = { 1.0f, 0.0f, 0.0f };  // カメラローカルの右＝トロッコの進行方向
        params.directionality = 0.85f;
        params.waveform = ShakeWaveform::Kick;
        params.space = ShakeSpace::CameraLocal;
        return params;
    }

    class StationSlowdownEffectFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "StationSlowdownEffect"; }

        /// @details シーンの OnInitialize() が終わった後のフックなので、この時点なら
        ///          スタミナ・列車・マップが揃っている。速度計は SpeedGaugeFeature が
        ///          先に作っている（登録順が先なので PostSceneInitialize も先）。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            hunger_ = ctx.gameObjectManager->FindFirstComponent<GameComponents::HungerComponent>();
            train_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::TrainMovementComponent>();
            // 駅チップを跳ねさせるために見ている。無くても残りの層は鳴る
            mapView_ = ctx.gameObjectManager->FindFirstComponent<GameComponents::MapViewComponent>();
            gauge_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::SpeedGaugeUIComponent>();
            audio_ = ctx.engine ? ctx.engine->GetService<AudioSystem>() : nullptr;

            if (!hunger_ || !train_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "StationSlowdownEffect: 必要なコンポーネントが見つからないため演出を出しません");
                return;
            }

            hunger_->SetStationEnteredCallback(
                [this](std::int32_t stationX, std::int32_t stationZ) {
                    OnStationEntered(stationX, stationZ);
                });
        }

        /// @details GameObject が消える前に、スタミナ側が握っているコールバックを外す。
        ///          Feature はシーンより後に壊れるので、外さないと解放済みの this を呼びうる。
        void Finalize(SceneContext&) override
        {
            if (hunger_) {
                hunger_->SetStationEnteredCallback(nullptr);
            }
            hunger_ = nullptr;
            train_ = nullptr;
            mapView_ = nullptr;
            gauge_ = nullptr;
            audio_ = nullptr;
        }

    private:
        /// @brief 先頭が駅前レールへ入った瞬間に呼ばれる。引数は駅チップのマス座標
        /// @note この時点ではまだ速度は落ちていない。落ちる先は現在の最低速度なので、
        ///       落差は「今の速度 - 最低速度」で先に分かる。
        void OnStationEntered(std::int32_t stationX, std::int32_t stationZ)
        {
            if (!cvEnabled.Get() || !train_) {
                return;
            }

            const float droppedCellsPerSecond =
                train_->GetMoveSpeed() - train_->GetMinMoveSpeed();
            if (droppedCellsPerSecond <= std::max(cvMinDropCells.Get(), 0.0f)) {
                // 落ちないなら説明することが無い。駅そのものの反応は連結時に別途出る
                return;
            }

            // 落差が小さいほど弱くはするが、鳴らすと決めた以上は下限を残す。
            // 見えるか見えないかの演出は「出ていない」と同じで、初見への説明にならない。
            // 下限は SpeedGaugeUIComponent::PlaySlowdownFlash() の内部の下限と揃えてある
            const float strength = std::max(std::clamp(
                droppedCellsPerSecond / std::max(cvFullDropCells.Get(), 0.01f), 0.0f, 1.0f),
                0.35f);

            // 原因。この駅がやった、と画面の中で名指しする
            if (mapView_) {
                mapView_->PlayStationPop(stationX, stationZ);
            }
            // 衝撃。急に速度を失って進行方向へつんのめる
            const float shake = strength * std::max(cvShakeScale.Get(), 0.0f);
            if (shake > 0.0f) {
                CameraShake::Play(MakeLurchShake(shake));
            }
            // 数値。何が減ったのかを速度計で名指しする
            if (gauge_) {
                gauge_->PlaySlowdownFlash(droppedCellsPerSecond);
            }
            // 音。因果を一番安く繋ぐ層なので、落差が小さくても音量までは絞りきらない
            if (audio_ && cvSeEnabled.Get()) {
                PlayParams params;
                params.bus = AudioBus::SE;
                params.volume = cvSeVolume.Get() * (0.6f + 0.4f * strength);
                params.pitch = std::max(cvSePitch.Get(), 0.25f);
                audio_->PlayOneShot(kCouplingSe, params);
            }

            Logger::GetInstance().Infof(
                LogCategory::Game,
                "駅で減速: {:.3f} -> {:.3f} マス/秒 (落差={:.3f}, 強さ={:.2f})",
                train_->GetMoveSpeed(), train_->GetMinMoveSpeed(),
                droppedCellsPerSecond, strength);
        }

        GameComponents::HungerComponent* hunger_ = nullptr;
        GameComponents::TrainMovementComponent* train_ = nullptr;
        GameComponents::MapViewComponent* mapView_ = nullptr;
        GameComponents::SpeedGaugeUIComponent* gauge_ = nullptr;
        AudioSystem* audio_ = nullptr;
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateStationSlowdownEffectFeature()
{
    return std::make_unique<StationSlowdownEffectFeature>();
}
