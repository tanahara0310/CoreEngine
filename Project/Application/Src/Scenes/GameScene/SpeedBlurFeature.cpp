#include "pch.h"
#include "SpeedBlurFeature.h"

#include "Components/Train/TrainMovementComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <memory>

using namespace CoreEngine;

namespace {

    // ──────────────────────────────────────────────────────────
    // 速さに連動したモーションブラー
    // ──────────────────────────────────────────────────────────
    // 速度が上がるほどシャッター開角度を開き、駅で速度を失うと一気に閉じる。
    // 速度計の数字と違って画面全体が言うので、視線がどこにあっても届く。
    //
    // ■ 駅の減速でいちばん効く層
    //   数字は 1 秒近くかけて落ちるが、ブラーは ResponseSeconds で閉じきる。
    //   「さっきまで流れていた地面が急に止まって見える」ほうが、
    //   桁が落ちるより先に、速さを失ったことを体で分からせてくれる。
    //
    // ■ 止まるのはゼロではなく「そのときの最低速度」
    //   最低速度は敷いたレール数で上がっていくので、駅で落ちてもブラーは完全には消えない。
    //   ゲームが進むほど「落ちても以前より速い」ことが、そのまま画に出る。

    CVar<bool> cvEnabled{
        "Game.SpeedBlur.Enabled", false,
        "トロッコの速さに合わせて画面にモーションブラーを掛ける" };

    CVar<float> cvStartSpeedCells{
        "Game.SpeedBlur.StartSpeedCells", 1.8f,
        "ブラーが出はじめる速度［マス/秒］。発車直後（既定 1.5）のすぐ上に置いてある。"
        "ここを上げすぎると、駅で落ちる前も後もブラーが 0 のままで減速が画に出ない",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> cvFullSpeedCells{
        "Game.SpeedBlur.FullSpeedCells", 6.0f,
        "ブラーが最大になる速度［マス/秒］。実測では 1 分半のプレイで 3 マス/秒あたり、"
        "駅を挟みながらの長い走行で 6〜7 マス/秒まで伸びる。"
        "最高速（32）まで比例させると、実際に出る速度域では 1 割も濃くならず何も見えない",
        CVarRange{ 0.5f, 40.0f } };

    CVar<float> cvMaxShutterAngle{
        "Game.SpeedBlur.MaxShutterAngle", 200.0f,
        "最大時のシャッター開角度［度］。180 で露光時間がフレームの半分（映画の標準）。"
        "上げるほど流れるが、上げすぎると地面の模様が溶けて距離目盛りが読めなくなる",
        CVarRange{ 0.0f, 360.0f } };

    CVar<float> cvResponseSeconds{
        "Game.SpeedBlur.ResponseSeconds", 0.12f,
        "ブラーの濃さが目標へ追いつくまでの時間［秒］。"
        "0 にすると駅の減速で 1 フレームで消える。短いほど減速が鋭く見える",
        CVarRange{ 0.0f, 1.0f } };

    /// @brief current から target へ、1 フレームぶんの上限 maxDelta まで近づける
    float MoveTowards(float current, float target, float maxDelta)
    {
        const float diff = target - current;
        if (std::abs(diff) <= maxDelta) {
            return target;
        }
        return current + (diff > 0.0f ? maxDelta : -maxDelta);
    }

    class SpeedBlurFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "SpeedBlur"; }

        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }
            train_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::TrainMovementComponent>();
            postEffects_ = ctx.engine ? ctx.engine->GetService<PostEffectManager>() : nullptr;
        }

        void Update(SceneContext&, SceneUpdatePhase phase) override
        {
            // 列車の速度が今フレームぶん確定してから読む
            if (phase != SceneUpdatePhase::PostObjectUpdate || !train_ || !postEffects_) {
                return;
            }
            if (!cvEnabled.Get()) {
                Release();
                return;
            }

            const float start = cvStartSpeedCells.Get();
            const float full = std::max(cvFullSpeedCells.Get(), start + 0.01f);
            const float target =
                std::clamp((train_->GetMoveSpeed() - start) / (full - start), 0.0f, 1.0f);

            const float response = cvResponseSeconds.Get();
            blur_ = (response <= 0.0f)
                ? target
                : MoveTowards(blur_, target, std::max(Time::DeltaTime(), 0.0f) / response);

            if (blur_ <= 0.0f) {
                Release();
                return;
            }

            Borrow();
            WriteShutterAngle(cvMaxShutterAngle.Get() * blur_);
            if (!enabled_) {
                postEffects_->SetEffectEnabled(PostEffectNames::MotionBlur, true);
                enabled_ = true;
            }
        }

        /// @details 借りたエンジン設定を返してからシーンを出る。
        ///          戻さないと、次のシーンがこのステージのブラーを引き継いでしまう。
        void PostSceneFinalize(SceneContext&) override
        {
            Release();
            train_ = nullptr;
            postEffects_ = nullptr;
        }

    private:
        /// @brief シャッター開角度の CVar を借りて、借りた時点の値を控える
        void Borrow()
        {
            if (held_) {
                return;
            }
            shutterAngle_ = CVarRegistry::Get().Find("r.MotionBlur.ShutterAngle");
            const float* current = shutterAngle_ ? shutterAngle_->AsFloat() : nullptr;
            if (!current) {
                shutterAngle_ = nullptr;  // 名前違い・型違い。黙って手を出さない
                return;
            }
            originalShutterAngle_ = *current;
            originalEnabled_ = postEffects_->IsEffectEnabled(PostEffectNames::MotionBlur);
            held_ = true;
        }

        void WriteShutterAngle(float value)
        {
            if (held_ && shutterAngle_) {
                // CVar 側が同値の書き込みを弾くので、毎フレーム呼んでも通知は走らない
                shutterAngle_->SetFromPointer(&value);
            }
        }

        /// @brief 借りた設定を元へ戻し、ブラーを切る
        void Release()
        {
            if (held_) {
                if (shutterAngle_) {
                    shutterAngle_->SetFromPointer(&originalShutterAngle_);
                }
                if (postEffects_) {
                    postEffects_->SetEffectEnabled(
                        PostEffectNames::MotionBlur, originalEnabled_);
                }
                held_ = false;
            }
            shutterAngle_ = nullptr;
            enabled_ = false;
            blur_ = 0.0f;
        }

        GameComponents::TrainMovementComponent* train_ = nullptr;
        PostEffectManager* postEffects_ = nullptr;

        ICVar* shutterAngle_ = nullptr;      ///< 借りている r.MotionBlur.ShutterAngle
        float originalShutterAngle_ = 0.0f;  ///< 借りた時点の値。返すときに書き戻す
        bool originalEnabled_ = false;       ///< 借りた時点でブラーが有効だったか
        bool held_ = false;                  ///< 借りているか
        bool enabled_ = false;               ///< こちらの都合でブラーを点けているか

        float blur_ = 0.0f;                  ///< 今の濃さ 0〜1。目標へ滑らかに追いつく
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateSpeedBlurFeature()
{
    return std::make_unique<SpeedBlurFeature>();
}
