#include "pch.h"
#include "RockBreakDebris.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Particle/ParticleSystem.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/MainModule.h"
#include "Scene/Feature/ISceneFeature.h"
#include "Utility/Logger/Logger.h"

#include <memory>

using namespace CoreEngine;

namespace {

    /// 破片のモデル。particle.obj は 0.4 角のボクセル片
    /// @note モデルとテクスチャはプリセットに含まれない（インスペクタからも変えられない）ので、
    ///       ここで指定してからプリセットを読む。
    constexpr const char* kDebrisModel = "particle.obj";

    /// 破片のテクスチャ
    /// @note 未指定だとパーティクルシステム既定の circle.png が乗って白くぼやける。
    ///       モデル付属のパレットを明示的に差す。
    constexpr const char* kDebrisTexture = "particle.png";

    // ──────────────────────────────────────────────────────────
    // 見た目と動きのパラメータはプリセット（json）が持つ
    // ──────────────────────────────────────────────────────────
    // インスペクタでこのパーティクル（RockBreakDebris）を選び、「プリセット」欄から
    // 読み込み・上書き保存（Ctrl+S）ができる。値を触るときに知っておくと迷わない点：
    //
    // ■ 床の高さ 0.5（床との衝突、1マスの大きさが1の場合）
    //   BlockModelLayout の地面上面と同じ高さ。ground.obj の高さ（1.6）を
    //   共通スケール（0.625）で1マスへ変換する。床は1枚の水平面なので、水のマス（水面 0.15）や
    //   空きマスの上へ飛んだ破片もこの高さで止まる。
    //
    // ■ 重力係数（メイン）は 1.0 のままにすること
    //   外力モジュールの重力は、この係数との掛け算で効く。0 に戻すと重力が丸ごと死ぬ。
    //
    // ■ ノイズは無効にしてある
    //   既定では有効（強度 1.0）で、切らないと着地した破片が床の上で揺れ続ける。
    //
    // ■ 初期回転が 180 度 ± 100% なのは意図的
    //   メインのランダムは base ± base×randomness なので、基準を 0 にすると振れ幅も 0 になる。
    //   180 度を基準に 100% 振ることで 0〜360 度の一様乱数にしている。
    //
    // ■ 消え際はサイズではなく色（アルファ）で消す
    //   ブレンドが「通常」なので、アルファはそのまま透明度として効く。
    //   色の「変化を始める割合」がフェードアウトの開始タイミング。
    //
    // ■ 色は破壊エフェクトの指定色（#644B2C）に合わせてある
    //   破片モデル particle.obj の UV は particle.png のほぼ白いパレット色（sRGB 252,250,242）を
    //   指すので、メインの「色」でそれを打ち消して指定色（sRGB 100,75,44）へ寄せている。
    //   終了色も RGB を同じ値にしてある（違う値にすると消えながら色が変わる）。
    //   モデルかテクスチャを差し替えたら、この打ち消し係数を引き直すこと。
    //
    // ■ ブレンドは「通常」（半透明）
    //   「なし」（不透明）に戻すとアルファが効かず、消え際が透けずに残る。
    //   半透明では深度書き込みが切られるので、破片どうしの前後関係は描画順まかせになり、
    //   DXR の影キャスターからも外れる（RayTracingSubsystem は不透明の粒だけを拾う）。

    /// 破片の見た目・動きを決めるプリセット
    constexpr const char* kPresetPath =
        "Application/Assets/Presets/Particle/RockBreakDebris.json";

    /// 着弾位置（岩の足元）から、破片を湧かせる高さ。岩の高さの真ん中あたり
    /// @note ここは「岩のどこから出すか」という置き場所の話なのでコード側に残す。
    ///       プリセットのエミッター位置は再生のたびに上書きされる。
    constexpr float kEmitterHeightOffset = 0.45f;

    // ──────────────────────────────────────────────────────────
    // Feature
    // ──────────────────────────────────────────────────────────

    class RockBreakDebrisFeature;

    /// いま生きている Feature。PlayRockBreakDebris() の宛先
    /// @note シーンを抜けると null に戻る（タイトル・リザルトでは鳴らさない）
    RockBreakDebrisFeature* g_activeFeature = nullptr;

    /// @brief 岩の破片を出すモデルパーティクルを、シーンへ 1 つ置いておく Feature
    class RockBreakDebrisFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "RockBreakDebris"; }

        void Initialize(SceneContext& context) override
        {
            particleSystem_ = CreateParticleSystem(context);
            if (particleSystem_) {
                g_activeFeature = this;
            }
        }

        /// @brief シーン終了時。GameObject が消える前に受け口を閉じる
        void Finalize(SceneContext&) override
        {
            if (g_activeFeature == this) {
                g_activeFeature = nullptr;
            }
            particleSystem_ = nullptr;
        }

        /// @brief 指定位置へ破片を 1 回分出す
        void Play(const Vector3& impactPosition)
        {
            if (!particleSystem_) {
                return;
            }

            particleSystem_->SetEmitterPosition({
                impactPosition.x,
                impactPosition.y + kEmitterHeightOffset,
                impactPosition.z });

            // Clear() は呼ばない。連続で壊したとき、前の岩の破片を空中で消さずに
            // 落ち切らせる（プリセットの最大パーティクル数に数回分の余裕がある）。
            particleSystem_->GetMainModule().Restart();
            particleSystem_->GetEmissionModule().Play();
        }

    private:
        /// @brief 破片用のパーティクルシステムを生成する
        /// @return 生成できなければ nullptr（この Feature は以降なにもしない）
        static ParticleSystem* CreateParticleSystem(SceneContext& context);

        /// 破片を出すパーティクルシステム（所有は GameObjectManager）
        ParticleSystem* particleSystem_ = nullptr;
    };

    ParticleSystem* RockBreakDebrisFeature::CreateParticleSystem(SceneContext& context)
    {
        auto* engine = context.engine;
        if (!engine || !context.gameObjectManager) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "RockBreakDebris: シーンのコンテキストが揃っていないので破片を作れません");
            return nullptr;
        }

        auto* dxCommon = engine->GetService<GraphicsCore>();
        auto* resourceFactory = engine->GetService<ResourceFactory>();
        auto* modelManager = engine->GetService<ModelManager>();
        if (!dxCommon || !resourceFactory || !modelManager) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "RockBreakDebris: 必要なサービスが揃っていないので破片を作れません");
            return nullptr;
        }

        // GetModelResource は読み込み済みのものを引くだけで、自分では読まない。
        // particle.obj はこのエフェクト専用で誰も先に読まないので、ここで読ませる。
        modelManager->PreloadModels({ kDebrisModel });

        ModelResource* modelResource = modelManager->GetModelResource(kDebrisModel);
        if (!modelResource) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "RockBreakDebris: モデルを読めませんでした: {}", kDebrisModel);
            return nullptr;
        }

        auto* particleSystem =
            context.gameObjectManager->AddObject(std::make_unique<ParticleSystem>());
        particleSystem->Initialize(dxCommon, resourceFactory, "RockBreakDebris");
        particleSystem->SetTexture(kDebrisTexture);

        // SetModelResource が描画モードを Model へ切り替える（＝ModelParticle パスへ乗る）
        particleSystem->SetModelResource(modelResource);

        // ブレンドモードも含め、見た目と動きはすべてプリセットが持つ
        if (!particleSystem->LoadPreset(kPresetPath)) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "RockBreakDebris: プリセットを読めませんでした: {}", kPresetPath);
            return nullptr;
        }

        return particleSystem;
    }
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateRockBreakDebrisFeature()
{
    return std::make_unique<RockBreakDebrisFeature>();
}

void GameComponents::PlayRockBreakDebris(const CoreEngine::Vector3& impactPosition)
{
    if (g_activeFeature) {
        g_activeFeature->Play(impactPosition);
    }
}
