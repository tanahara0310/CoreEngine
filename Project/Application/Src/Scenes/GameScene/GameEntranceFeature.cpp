#include "pch.h"
#include "GameEntranceFeature.h"

#include "Audio/AudioSystem.h"
#include "Camera/Rig/CameraRig.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "Components/UI/ObjectiveSignComponent.h"
#include "Components/UI/PauseMenuUIComponent.h"
#include "Components/UI/SpeedGaugeUIComponent.h"
#include "Components/UI/StaminaGaugeUIComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Text3D/Text3DObject.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Math/Easing/EasingUtil.h"
#include "RailDirectionGuideFeature.h"
#include "Scene/Feature/ISceneFeature.h"
#include "UI/UIImage.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace CoreEngine;

namespace
{
    /// 真上から見下ろす開幕のリグ。構図は Presets/CameraRigs/Entrance_Sky.json が持つ
    constexpr const char* kSkyRigName = "Entrance_Sky";
    /// 通常のゲーム構図。_camera.json の startupRigName と同じもの
    constexpr const char* kPlayRigName = "GamePlay";
    /// MapViewComponent が置く距離目盛りのオブジェクト名の頭
    constexpr const char* kMarkerNamePrefix = "DistanceMarker";
    /// 看板の入れ物に使う透明画像と、開幕の白幕に使う白一色の画像（どちらも同じ dim.png）
    constexpr const char* kSignRootTexture = "Application/Assets/Textures/Pause/dim.png";
    /// 白幕の描画順。ポーズメニュー（2000）より手前へ出す
    constexpr int kWhiteoutSortOrder = 3000;
    /// 基準解像度。白幕はこの大きさで画面を覆う（変更禁止）
    constexpr float kCanvasWidth = 1920.0f;
    constexpr float kCanvasHeight = 1080.0f;

    constexpr const char* kGoalSePath = "Application/Assets/Sounds/SE/decision.mp3";
    constexpr const char* kCallSePath = "Application/Assets/Sounds/SE/title_bound.mp3";

    /// 白幕が晴れ切ったと見なす余白
    constexpr float kWhiteoutClearMargin = 0.05f;
    /// 1 フレームで進める上限 [秒]。シーン読み込み直後の跳ねで演出が飛ぶのを防ぐ
    constexpr float kMaxStepSeconds = 0.1f;
    /// 到達した目盛りが白から達成色へ落ち着くまでの秒数
    constexpr float kReachedFlashSeconds = 0.6f;
    /// 次の目標の目盛りが脈打つはやさ [rad/秒]
    constexpr float kTargetPulseSpeed = 4.0f;
    /// 脈打ちの振れ幅（1.0 に対する比率）
    constexpr float kTargetPulseAmount = 0.35f;
    /// HUD が 1 枚ずつ遅れて出てくる間隔 [秒]。同時に出すより «次々と揃う» ほうが目で追える
    constexpr float kHudStagger = 0.10f;

    // ──────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターの「ゲーム設定」に出る）
    // ──────────────────────────────────────────────────────────
    CVar<bool> cvEnabled{
        "Game.Entrance.Enabled", true,
        "突入演出（雲海ブレイクとカメラの降下）を出すか。"
        "切っても目標看板と目標の進行はそのまま動く" };

    CVar<float> cvCloudSeconds{
        "Game.Entrance.CloudSeconds", 2.05f,
        "開幕の白幕（雲の中）が晴れるまでの秒数",
        CVarRange{ 0.2f, 8.0f } };

    CVar<float> cvCameraDelay{
        "Game.Entrance.CameraDelay", 0.20f,
        "空のリグからゲーム構図へ降り始めるまでの秒数",
        CVarRange{ 0.0f, 4.0f } };

    CVar<float> cvCameraBlendSeconds{
        "Game.Entrance.CameraBlendSeconds", 2.10f,
        "空のリグからゲーム構図へ降り切るまでの秒数",
        CVarRange{ 0.1f, 8.0f } };

    CVar<float> cvSignDelay{
        "Game.Entrance.SignDelay", 2.20f,
        "最初の目標看板が降りてくる時刻 [秒]",
        CVarRange{ 0.0f, 10.0f } };

    CVar<float> cvCallDelay{
        "Game.Entrance.CallDelay", 4.75f,
        "「つなげ！！」を叩き込む時刻 [秒]",
        CVarRange{ 0.0f, 12.0f } };

    CVar<float> cvHudRevealOffset{
        "Game.Entrance.HudRevealOffset", 0.15f,
        "「つなげ！！」から何秒後に HUD（スタミナ・速度計・操作ヒント・レールの矢印）が"
        "出てくるか。演出が終わるまでは引っ込んでいる",
        CVarRange{ 0.0f, 6.0f } };

    CVar<float> cvHudRevealSeconds{
        "Game.Entrance.HudRevealSeconds", 0.55f,
        "HUD が定位置へ滑り込む（矢印は伸び上がる）までの秒数（1 つあたり）",
        CVarRange{ 0.05f, 3.0f } };

    CVar<int> cvGoalStep{
        "Game.Goal.StepMeters", 500,
        "目標距離の刻み [m]。500 なら 500 → 1000 → 1500 … と続く",
        CVarRange{ 10.0f, 2000.0f } };

    CVar<Vector4> cvReachedColor{
        "Game.Goal.MarkerReachedColor", { 0.02f, 0.22f, 0.03f, 1.0f },
        "到達済みの目標地点の目盛りの色。3D テキストはリニア値なので、"
        "0.25 を超えた成分は昼の露出で白へ飽和する" };

    CVar<Vector4> cvTargetColor{
        "Game.Goal.MarkerTargetColor", { 0.25f, 0.010f, 0.008f, 1.0f },
        "次の目標地点の目盛りの色（脈打つ）。同じくリニア値で入れること。"
        "既定は赤（画面では (230, 60, 55) ほど）で、脈の山では白へ寄って光る" };

    Vector4 Lerp(const Vector4& from, const Vector4& to, float t)
    {
        return { from.x + (to.x - from.x) * t,
                 from.y + (to.y - from.y) * t,
                 from.z + (to.z - from.z) * t,
                 from.w + (to.w - from.w) * t };
    }

    // ──────────────────────────────────────────────────────────
    // Feature
    // ──────────────────────────────────────────────────────────

    /// @brief 突入演出と、500m 刻みの目標提示をまとめて指揮する Feature
    class GameEntranceFeature final : public ISceneFeature
    {
    public:
        const char* GetName() const override { return "GameEntrance"; }

        /// @details シーンの OnInitialize() が終わった後に呼ばれるフックなので、
        ///          この時点なら列車も距離目盛りの持ち主も既に生成されている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }
            engine_ = ctx.engine;
            train_ = ctx.gameObjectManager
                ->FindFirstComponent<GameComponents::TrainMovementComponent>();
            if (!train_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "GameEntranceFeature: 列車が見つからないので目標の進行は止まります");
            }
            railBuilder_ = ctx.gameObjectManager
                ->FindFirstComponent<GameComponents::RailBuilderComponent>();
            if (!railBuilder_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "GameEntranceFeature: レールカーソルが見つからないので"
                    "雲の中でも操作できてしまいます");
            }
            // UI もトーンマップ前のバッファへ描かれるので、掛かる露出を打ち消す
            if (auto* postEffects = engine_ ? engine_->GetService<PostEffectManager>() : nullptr) {
                toneMapping_ = postEffects->GetEffect<ToneMapping>(PostEffectNames::ToneMapping);
            }
            if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
                audioSystem_ = audioSystem;
            }

            auto* root = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
            if (root) {
                root->Initialize(kSignRootTexture, "ObjectiveSignRoot");
                root->SetSerializeEnabled(false);
                sign_ = root->AddComponent<GameComponents::ObjectiveSignComponent>();
            }
            if (!sign_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "GameEntranceFeature: 目標看板を作れませんでした");
            }

            goalMeters_ = GoalStep();

            // 開幕の白幕。1 フレーム目から白いよう、ここで作って濃さも入れておく
            if (auto* sheet = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>())) {
                sheet->Initialize(kSignRootTexture, "EntranceWhiteout");
                sheet->SetSerializeEnabled(false);
                sheet->SetAnchor(UIAnchor::Center);
                sheet->SetPivot({ 0.5f, 0.5f });
                sheet->SetAnchoredPosition({ 0.0f, 0.0f });
                sheet->SetSize({ kCanvasWidth, kCanvasHeight });
                sheet->SetSortOrder(kWhiteoutSortOrder);
                sheet->SetActive(false);
                whiteout_ = sheet;
            }
            if (cvEnabled.Get()) {
                ApplyWhiteout(0.0f);
                SetControlLocked(true);
            }
        }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override
        {
            if (phase == SceneUpdatePhase::FrameStart) {
                UpdateEntrance(ctx);
                UpdateGoal();
                return;
            }
            if (phase == SceneUpdatePhase::PostLogic) {
                // 目盛りの色は MapView が書いた後に上書きする。
                // MapView は生成時に一度だけ白を入れるので、毎フレーム上書きしても衝突しない
                UpdateDistanceMarkers(ctx);
            }
        }

        void Finalize(SceneContext&) override
        {
            // 白幕はシーンの GameObject なのでシーンと一緒に消える。
            // CVar は 1 つも借りていないので、ここで戻すものは無い。
            whiteout_ = nullptr;
            whiteoutActive_ = false;
        }

    private:
        int GoalStep() const { return std::max(1, cvGoalStep.Get()); }

        /// @brief 開幕の白幕の濃さを、演出の進み具合に合わせて更新する
        /// @param progress 0 = 開幕（雲の中）／1 = 晴れ切った
        ///
        /// @details **ここで CVar を 1 つも触らないのが肝心。** 最初はフォグ（Game.Fog.*）を
        ///          毎フレーム書き換えて雲を沈める作りにしていたが、CVar の自動保存が
        ///          「最後の変更から 0.3 秒後」に走るため、演出中に保存が起きて
        ///          `r.Fog.*` 一式（`r.Fog.Enabled: true` を含む）が CVars.json へ焼き付いた。
        ///          既定では `r.Fog.Enabled` は false なので、以降タイトル画面まで
        ///          フォグが掛かって白っぽくなる。白幕は UIImage の色なので保存されない。
        void ApplyWhiteout(float progress)
        {
            if (!whiteout_) {
                return;
            }
            const float eased =
                EasingUtil::Apply(std::clamp(progress, 0.0f, 1.0f),
                                  EasingUtil::Type::EaseInOutCubic);
            const float alpha = 1.0f - eased;
            whiteoutActive_ = alpha > 0.0f;
            whiteout_->SetActive(whiteoutActive_);
            // 自動露出の打ち消しは掛けない。白飛びさせたいので飽和したままでよい
            whiteout_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
        }

        /// @brief 雲の中にいる間、プレイヤーの操作を止める
        ///
        /// @details 止めるのはレールカーソル（RailBuilderComponent）だけでよい。
        ///          列車はプレイヤーが最初のレールを敷くまで発車しないので、
        ///          カーソルを止めればゲームの進行ごと待たせられる。
        ///          コンポーネントごと切るため、移動・敷設・Undo・投石がまとめて
        ///          効かなくなり、長押しの溜め（buildPushTimer_ など）も進まない。
        ///          ゲームオーバー時に GameManagerComponent がやっているのと同じ止め方。
        void SetControlLocked(bool locked)
        {
            if (!railBuilder_ || controlLocked_ == locked) {
                return;
            }
            controlLocked_ = locked;
            railBuilder_->SetEnabled(!locked);
            if (locked) {
                PrimeCursorScale();
            }
        }

        /// @brief 止めている間ぶんだけ、カーソルの大きさを先に入れておく
        ///
        /// @details RailBuilderComponent は毎フレーム自分で拡縮を書くが、止めている間は
        ///          それが回らず、Transform の初期値（1 倍）のまま小さく映ってしまう。
        ///          白幕は終わりぎわがほとんど透けるので、そこで小さい矢印が見えないよう
        ///          1 マスぶんの大きさをここで入れておく（脈打ちは操作を返してから始まる）。
        void PrimeCursorScale()
        {
            auto* transform = railBuilder_->Sibling<TransformComponent>();
            if (!transform) {
                return;
            }
            const float scale = GameComponents::BlockModelLayout::GetScale(
                GameComponents::GameSettings::GridSize.Get());
            transform->Get().scale = { scale, scale, scale };
        }

        void UpdateEntrance(SceneContext& ctx)
        {
            if (sign_) {
                sign_->SetExposureScale(
                    toneMapping_ ? std::exp2(-toneMapping_->GetAutoExposureEV()) : 1.0f);
            }
            if (entranceDone_) {
                return;
            }
            const bool playCinematic = cvEnabled.Get();
            elapsed_ += std::clamp(Time::DeltaTime(), 0.0f, kMaxStepSeconds);

            // ---- 雲海ブレイク ----
            if (playCinematic) {
                const float cloudSeconds = std::max(0.01f, cvCloudSeconds.Get());
                const float progress = elapsed_ / cloudSeconds;
                if (progress < 1.0f) {
                    ApplyWhiteout(progress);
                } else if (whiteoutActive_) {
                    ApplyWhiteout(1.0f);
                }

                // ---- カメラ：真上のリグ → ゲーム構図 ----
                if (!skyRigStarted_) {
                    // 起動時のリグ（_camera.json の startupRigName）より確実に後に
                    // 割り込むため、初期化フックではなく最初の更新で握る
                    skyRigStarted_ = true;
                    if (!CameraRig::Activate(kSkyRigName)) {
                        Logger::GetInstance().Warnf(
                            LogCategory::Game,
                            "GameEntranceFeature: リグ {} が無いのでカメラ演出は飛ばします",
                            kSkyRigName);
                        playRigStarted_ = true;   // 降下も要らない
                    }
                }
                if (!playRigStarted_ && elapsed_ >= cvCameraDelay.Get()) {
                    playRigStarted_ = true;
                    CameraRigActivateOptions options;
                    options.blendSeconds = std::max(0.01f, cvCameraBlendSeconds.Get());
                    CameraRig::Activate(kPlayRigName, options);
                }
            }

            // ---- 操作を返す ----
            // 白幕が晴れて（＝ワールドが見えて）からカーソルを動かせるようにする。
            // 「つなげ！！」や HUD の登場まで待たせると、見えているのに動かせない
            // 間ができてしまうので、締めの演出より先に返す。
            // 演出を切っている（Game.Entrance.Enabled が false）ときは白幕自体が
            // 出ないため、ここは初回で素通りする
            if (controlLocked_ && !whiteoutActive_) {
                SetControlLocked(false);
            }

            // ---- もくひょう看板 ----
            const float signDelay = playCinematic ? cvSignDelay.Get() : 0.3f;
            if (!signShown_ && elapsed_ >= signDelay) {
                signShown_ = true;
                if (sign_) {
                    sign_->Show(static_cast<std::uint32_t>(goalMeters_));
                }
            }

            // ---- つなげ！！ ----
            const float callDelay = playCinematic ? cvCallDelay.Get() : 1.6f;
            if (!callPlayed_ && elapsed_ >= callDelay) {
                callPlayed_ = true;
                if (sign_) {
                    sign_->PlayStartCall();
                }
                CameraShake::Play(CameraShakePresets::Landing());
                PlaySe(kCallSePath);
            }

            // ---- HUD の登場 ----
            UpdateHudReveal(ctx, callDelay);

            if (callPlayed_ && hudRevealDone_ && !whiteoutActive_
                && elapsed_ >= callDelay + kWhiteoutClearMargin) {
                entranceDone_ = true;
            }
        }

        /// @brief HUD とレールの矢印を演出中は隠し、締めの合図に合わせて出す
        /// @param callDelay 「つなげ！！」の時刻 [秒]。ここを基準に出す
        /// @details 各 HUD は自分の板幅から寄せ幅を決めるので、ここが渡すのは進み具合だけ。
        ///          EaseOutBack を通した値をそのまま渡して、行き過ぎて戻る手応えを出す。
        void UpdateHudReveal(SceneContext& ctx, float callDelay)
        {
            if (hudRevealDone_) {
                return;
            }
            // HUD は各 Feature の PostSceneInitialize で作られる。こちらのほうが
            // 登録が先なので、初期化時点では見つからない。毎フレーム引き直す
            if (!stamina_ && ctx.gameObjectManager) {
                stamina_ = ctx.gameObjectManager
                    ->FindFirstComponent<GameComponents::StaminaGaugeUIComponent>();
            }
            if (!speedGauge_ && ctx.gameObjectManager) {
                speedGauge_ = ctx.gameObjectManager
                    ->FindFirstComponent<GameComponents::SpeedGaugeUIComponent>();
            }
            if (!pauseMenu_ && ctx.gameObjectManager) {
                pauseMenu_ = ctx.gameObjectManager
                    ->FindFirstComponent<GameComponents::PauseMenuUIComponent>();
            }

            const float start = callDelay + cvHudRevealOffset.Get();
            const float span = std::max(0.05f, cvHudRevealSeconds.Get());
            const auto revealAt = [&](float stagger) {
                const float t =
                    std::clamp((elapsed_ - start - stagger) / span, 0.0f, 1.0f);
                return EasingUtil::Apply(t, EasingUtil::Type::EaseOutBack);
            };

            // 床の矢印はワールド側なので、HUD の 1 枚目と同時でも読み分けられる
            GameComponents::SetRailDirectionGuideReveal(revealAt(0.0f));
            if (stamina_) { stamina_->SetIntroReveal(revealAt(0.0f)); }
            if (speedGauge_) { speedGauge_->SetIntroReveal(revealAt(kHudStagger)); }
            if (pauseMenu_) { pauseMenu_->SetIntroReveal(revealAt(kHudStagger * 2.0f)); }

            if (elapsed_ >= start + kHudStagger * 2.0f + span) {
                hudRevealDone_ = true;
            }
        }

        /// @brief 列車が目標を越えたら、目盛りへ色を付けて次の目標の看板を出す
        void UpdateGoal()
        {
            if (!train_) {
                return;
            }
            reachedFlash_ += std::clamp(Time::DeltaTime(), 0.0f, kMaxStepSeconds);

            // 地面の目盛りはワールド X をそのままメートルとして置かれている。
            // 列車の絶対位置で見るので、看板の数字と足元の数字が必ず一致する
            const float worldX = train_->GetWorldPosition().x;
            const int step = GoalStep();
            bool advanced = false;
            while (worldX >= static_cast<float>(goalMeters_)) {
                reachedMeters_ = goalMeters_;
                goalMeters_ += step;
                advanced = true;
            }
            if (!advanced) {
                return;
            }
            reachedFlash_ = 0.0f;
            if (sign_) {
                sign_->Show(static_cast<std::uint32_t>(goalMeters_));
            }
            PlaySe(kGoalSePath);
            Logger::GetInstance().Infof(
                LogCategory::Game,
                "GameEntrance: {}m 到達。つぎの目標は {}m", reachedMeters_, goalMeters_);
        }

        /// @brief 目標地点の目盛りだけ色を差し替える（それ以外は白のまま）
        void UpdateDistanceMarkers(SceneContext& ctx)
        {
            if (!ctx.gameObjectManager) {
                return;
            }
            const int step = GoalStep();
            const Vector4 plain{ 1.0f, 1.0f, 1.0f, 1.0f };
            const Vector4 reached = cvReachedColor.Get();

            // 次の目標は脈打たせて「ここを目指す」と分かるようにする
            const float pulse = 1.0f
                + std::sin(pulseTimer_ * kTargetPulseSpeed) * kTargetPulseAmount;
            const Vector4 target = [&] {
                const Vector4 c = cvTargetColor.Get();
                return Vector4{ c.x * pulse, c.y * pulse, c.z * pulse, c.w };
            }();
            // 到達した瞬間だけ白く光らせ、達成色へ落ち着かせる
            const float flash =
                std::clamp(reachedFlash_ / kReachedFlashSeconds, 0.0f, 1.0f);
            const Vector4 justReached = Lerp(plain, reached, flash);

            pulseTimer_ += std::clamp(Time::DeltaTime(), 0.0f, kMaxStepSeconds);

            for (const auto& object : ctx.gameObjectManager->GetAllObjects()) {
                if (!object || object->GetName().rfind(kMarkerNamePrefix, 0) != 0) {
                    continue;
                }
                auto* marker = dynamic_cast<Text3DObject*>(object.get());
                auto* transform = marker ? marker->GetComponent<TransformComponent>() : nullptr;
                if (!transform) {
                    continue;
                }
                const int meters =
                    static_cast<int>(std::lround(transform->Get().translate.x));
                if (meters <= 0 || meters % step != 0) {
                    marker->SetColor(plain);
                    continue;
                }
                if (meters == reachedMeters_) {
                    marker->SetColor(justReached);
                } else if (meters <= reachedMeters_) {
                    marker->SetColor(reached);
                } else if (meters == goalMeters_) {
                    marker->SetColor(target);
                } else {
                    marker->SetColor(plain);
                }
            }
        }

        void PlaySe(const char* path) const
        {
            if (audioSystem_) {
                audioSystem_->PlayOneShot(path, { .bus = AudioBus::SE });
            }
        }

        EngineSystem* engine_ = nullptr;
        AudioSystem* audioSystem_ = nullptr;
        ToneMapping* toneMapping_ = nullptr;
        GameComponents::TrainMovementComponent* train_ = nullptr;
        GameComponents::RailBuilderComponent* railBuilder_ = nullptr;
        GameComponents::ObjectiveSignComponent* sign_ = nullptr;
        GameComponents::StaminaGaugeUIComponent* stamina_ = nullptr;
        GameComponents::SpeedGaugeUIComponent* speedGauge_ = nullptr;
        GameComponents::PauseMenuUIComponent* pauseMenu_ = nullptr;
        UIImage* whiteout_ = nullptr;

        float elapsed_ = 0.0f;
        float pulseTimer_ = 0.0f;
        float reachedFlash_ = kReachedFlashSeconds;
        int goalMeters_ = 500;
        int reachedMeters_ = 0;

        bool whiteoutActive_ = false;
        bool controlLocked_ = false;
        bool skyRigStarted_ = false;
        bool playRigStarted_ = false;
        bool signShown_ = false;
        bool callPlayed_ = false;
        bool hudRevealDone_ = false;
        bool entranceDone_ = false;
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateGameEntranceFeature()
{
    return std::make_unique<GameEntranceFeature>();
}
