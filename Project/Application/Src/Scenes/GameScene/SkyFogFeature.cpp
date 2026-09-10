#include "pch.h"
#include "SkyFogFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Fog/Settings/FogCVars.h"
#include "Graphics/Light/Light.h"
#include "Graphics/Light/LightManager.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector4.h"
#include "Scene/Feature/ISceneFeature.h"
#include "Utility/CVar/CVar.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace CoreEngine;

namespace {

    // ──────────────────────────────────────────────────────────
    // 雲（フォグ）の調整値
    // ──────────────────────────────────────────────────────────
    // 狙いは「地面ブロックの柱が雲へ突き刺さって見える」。ステージの天面にはほとんど
    // 掛けないので、遠くのブロックも手前と同じようにはっきり見える。
    //
    // ■ 高さで切る（BaseHeight / HeightFalloff）
    //   密度モデルは rho(y) = Density * exp(-HeightFalloff * (y - BaseHeight))。
    //   BaseHeight が雲のいちばん濃い高さで、そこから上は HeightFalloff の速さで薄くなる。
    //   MapView が地面ブロックの底面から下へ 4.5m の柱（スカート）を吊るしているので、
    //   柱は y = +0.5m（天面）から -5.0m（底）まである。雲を y = -2.0m に置くと、
    //   上から 2.5m ぶんが立体として読めて、そこから 1m かけて雲へ溶けていく。
    //   ステージの外（何も無い空間）も雲で埋まるので、柱だけが雲海から生えて見える。
    //
    //   高さごとの減光（カメラの伏角 |dir.y| ≒ 0.85 のときの実効値）:
    //     +0.5m 天面 0.3% ／ -0.5m ブロック底 2.5% ／ -1.5m 21%
    //     -2.0m 50% ／ -2.5m 88% ／ -3.0m 99.8% ／ それ以下は不透明
    //
    //   最後の「-3.0m で不透明」は緩めないこと。吊るした柱は ground.obj を上下逆さに
    //   しているので、y = -3.31m から下は草の緑が出る。そこを隠しているのがこの濃さで、
    //   Density を下げたり HeightFalloff を上げたりすると緑が顔を出す。
    //   柱の長さを変えたときは MapView のインスペクター「草が出始める高さ」を見ること。
    //
    // ■ 何も無い空間を塗る（ApplyToSky）
    //   ステージの外は描画物が無い＝背景ピクセルなので、ここへ掛けないと
    //   雲がステージの真下にしか出ない。エンジン既定どおり有効にしておく。
    //
    // ■ この設定は「既定床が無い」前提
    //   GameScene・ResultScene とも SetDefaultGroundEnabled(false) を呼んである。
    //   床を戻すと y = 0 の板が雲より上に出るので、雲も柱も水場の滝も板に隠れて
    //   見えなくなる（全シーン一括の CVar r.Ground.Enable も同じ）。
    //
    // ■ HeightFalloff を下げすぎない／上げすぎない
    //   下げる（0.5 以下）と雲が上へ広がってステージの天面まで霞む。上げると雲へ溶ける
    //   幅が狭まり、柱が「白い床へ刺さった棒」に見えて厚みが出ない（4 で約 1m、
    //   6 以上はほぼ刃物の断面）。5.5m ある柱を使い切るには 2 前後。
    //
    // ■ 雲は白ではなく灰色にする（Color / Brightness）
    //   ステージの手前には 5m ごとの距離目盛り（MapView の「○○m」）が、地形の外＝雲だけを
    //   背にして寝ている。雲が白いと白い文字がそのまま溶けて読めない。
    //
    //   ここで効くのは「見た目の値」ではなく露出後の値。昼のゲームシーンを実測すると、
    //   リニア 0.44 で置いた雲が画面では 245/255 ＝ ほぼ白だった。ACES から逆算すると
    //   露出込みの実効ゲインは約 4.5 倍で、リニア 0.25 を超えると何色でも白へ飽和する。
    //   目安（Color 0.80 のときの Brightness → 画面の明るさ）:
    //     1.2 … 245（元の値。白）／ 0.55 … 245（まだ飽和）
    //     0.10 …  187（薄めの灰色。ここを既定にした）／ 0.05 … 141（中間の灰色）
    //   明るくするときは MapViewComponent の目盛りの文字色・縁取りも一緒に見ること。
    //
    // ■ 夜は雲も暗くする（NightBrightnessEV）
    //   フォグ色は Color × Brightness の絶対値で、FogManager がそのまま定数バッファへ
    //   入れる（時刻には追従しない）。一方でサーフェスは月光 80lx ＝ 太陽 100000lx の
    //   1/1250 まで落ち、自動露出は TimeOfDayFeature が上限 +7.5EV（≒×180）へ張り付か
    //   せる。昼と同じ色のままだと雲だけが露出に持ち上げられて白飛びし、ステージへ
    //   掛かるわずか 1〜3% の雲まで画面上では真っ白になる（＝ステージが霞んで見える）。
    //   そこで太陽高度から「夜の度合い」を出し、Brightness を EV で落として釣り合わせる。
    //
    // 値は CVars.json へ自動保存され、インスペクターの「ゲーム設定」から編集できる。

    CVar<bool> cvEnabled{
        "Game.Fog.Enabled", true,
        "ゲーム・リザルトシーンで雲を出す。切るとシーン開始前のフォグ設定へ戻る" };

    CVar<float> cvBaseHeight{
        "Game.Fog.BaseHeight", -2.0f,
        "雲のいちばん濃い高さ [m]。ここが雲海の面に見える。"
        "地面ブロックの底（-0.5m）から 1.5m 下げてあり、柱はここまでくっきり見える。"
        "上げるほどステージが雲へ沈み、下げるほど柱は長く見えるが底の草が出やすくなる。"
        "下げるときは MapView の「柱の長さ」も一緒に伸ばすこと",
        CVarRange{ -10.0f, 20.0f } };

    CVar<float> cvHeightFalloff{
        "Game.Fog.HeightFalloff", 2.2f,
        "上へどれだけ速く薄くなるか [1/m]。大きいほど雲へ溶ける幅が狭くなる。"
        "0.5 以下にするとステージの天面まで霞み、6 以上にすると柱が"
        "「白い床へ刺さった棒」に見える。5.5m ある柱を使い切るには 2 前後",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> cvDensity{
        "Game.Fog.Density", 1.3f,
        "BaseHeight の高さでの雲の濃さ [1/m]。上げると柱が下から雲へ埋もれていく。"
        "下げると柱の底（逆さに吊るした草の緑）が透けるので、下げ幅は控えめに",
        CVarRange{ 0.0f, 2.0f } };

    CVar<float> cvMaxOpacity{
        "Game.Fog.MaxOpacity", 1.0f,
        "雲の濃さの上限。1 未満にすると、いちばん濃いところでも下の色が透ける",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvStartDistance{
        "Game.Fog.StartDistance", 0.0f,
        "雲が効き始めるカメラからの距離 [m]。高さで切っているので通常は 0 でよい",
        CVarRange{ 0.0f, 200.0f } };

    CVar<Vector4> cvColor{
        "Game.Fog.Color", Vector4{ 0.80f, 0.81f, 0.84f, 1.0f },
        "雲の色（色味のみ。明るさは Brightness が持つ）。ほぼ無彩色にしてあり、"
        "青へ寄せるほど灰色に見えなくなる。Brightness と合わせて灰色を保つこと" };

    CVar<float> cvBrightness{
        "Game.Fog.Brightness", 0.10f,
        "雲の明るさ倍率。Color と掛けた値がリニアの雲色になる。"
        "昼の露出は実効 4.5 倍ほど掛かるので、0.3 も入れると白へ飽和して"
        "距離目盛りの「○○m」が読めなくなる。0.10 で画面上は 187/255 の灰色",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> cvSkyColorBlend{
        "Game.Fog.SkyColorBlend", 0.0f,
        "雲の色を空の色（大気散乱の輝度）へ寄せる量。0 なら Color × Brightness が"
        "そのまま出るので明るさを自分で決められる。上げると空へ自動で馴染むが、"
        "空の輝度は数十のオーダーなので一気に明るくなる",
        CVarRange{ 0.0f, 1.0f } };

    CVar<Vector4> cvSunTint{
        "Game.Fog.SunTint", Vector4{ 1.0f, 0.95f, 0.86f, 1.0f },
        "太陽方向での雲の色味（雲の色への倍率）" };

    CVar<float> cvSunGain{
        "Game.Fog.SunGain", 1.6f,
        "太陽方向での雲の明るさ倍率。1 で内散乱なし",
        CVarRange{ 1.0f, 8.0f } };

    CVar<float> cvSunExponent{
        "Game.Fog.SunExponent", 8.0f,
        "太陽まわりの光り方の鋭さ。大きいほど太陽の周りだけが狭く光る",
        CVarRange{ 1.0f, 128.0f } };

    CVar<float> cvNightBrightnessEV{
        "Game.Fog.NightBrightnessEV", -6.4f,
        "夜に Brightness を何段（EV）落とすか。0 にすると昼と同じ色のままになり、"
        "夜の自動露出に持ち上げられて雲が白飛びする。"
        "落とし込み量ではなく Brightness × 2^EV の積が夜の雲色なので、"
        "Brightness を変えたらここも同じ段数だけ逆へ動かすこと"
        "（Brightness 1.2・EV -10 の頃と同じ積になるのが 0.10・-6.4）",
        CVarRange{ -16.0f, 0.0f } };

    CVar<float> cvNightStartElevationDeg{
        "Game.Fog.NightStartElevationDeg", 5.0f,
        "暗くし始める太陽高度 [deg]。Game.StageLights.OnElevationDeg と揃えてある",
        CVarRange{ -20.0f, 30.0f } };

    CVar<float> cvNightFullElevationDeg{
        "Game.Fog.NightFullElevationDeg", -6.0f,
        "落としきる太陽高度 [deg]（-6 = 市民薄明の終わり）。"
        "Game.StageLights.FullElevationDeg と揃えてある",
        CVarRange{ -30.0f, 20.0f } };

    // ──────────────────────────────────────────────────────────
    // 夜の度合い
    // ──────────────────────────────────────────────────────────

    /// @brief 太陽の高度角 [deg]（太陽が無いシーンは昼として扱う）
    /// @note TimeOfDayFeature は地平線下でも太陽ライトの向きを更新し続けるので、
    ///       夜は素直に負の値になる。月は別ライトなのでここには出てこない
    float ComputeSunElevationDeg(LightManager& lightManager)
    {
        const Light* sun = lightManager.GetAtmosphereSunLight();
        if (!sun) {
            return 90.0f;
        }
        // ライト方向は「太陽 → 地表」なので、太陽を見る方向の Y が sin(高度)
        const Vector3 direction = Normalize(sun->direction);
        return std::asin(std::clamp(-direction.y, -1.0f, 1.0f))
            * MathCore::Constants::kRadToDeg;
    }

    /// @brief 太陽高度から夜の度合い（0 = 昼 / 1 = 夜）を求める
    /// @note StageLightsFeature::ComputeLitRatio と同じ式。灯りと雲の変わり方を揃える
    float ComputeNightFactor(float sunElevationDeg)
    {
        const float start = cvNightStartElevationDeg.Get();
        const float full = cvNightFullElevationDeg.Get();

        float t = (start > full)
            ? std::clamp((start - sunElevationDeg) / (start - full), 0.0f, 1.0f)
            : ((sunElevationDeg <= start) ? 1.0f : 0.0f);

        // 変わり始めと変わり終わりの角を丸める（線形だと切り替わりが唐突に見える）
        return t * t * (3.0f - 2.0f * t);
    }

    /// @brief 夜の度合いから Brightness へ掛ける倍率を求める
    /// @details 補間は EV（対数）で行う。明るさは対数で効くので線形に混ぜると、
    ///          薄明のあいだ雲だけが明るいまま取り残される。昼（0）では 1 倍で恒等
    float ComputeNightBrightnessScale(float nightFactor)
    {
        return std::exp2(cvNightBrightnessEV.Get() * nightFactor);
    }

    // ──────────────────────────────────────────────────────────
    // エンジン側フォグ（r.Fog.*）の読み書き
    // ──────────────────────────────────────────────────────────

    /// @brief このシーンが触る r.Fog.* 一式
    /// @details 退避と書き戻しを同じ形で行うためのまとめ。r.Fog.SkyDistance は
    ///          背景ピクセルのレイ長で、既定の 5000m のままで足りるので触らない。
    struct EngineFogState {
        bool    enabled;
        Vector4 color;
        float   colorIntensity;
        float   density;
        float   heightFalloff;
        float   heightRef;
        float   startDistance;
        float   maxOpacity;
        bool    applyToSky;
        float   skyColorBlend;
        Vector4 sunTint;
        float   sunGain;
        float   sunExponent;
    };

    /// @brief r.Fog.* の現在値を読み出す
    EngineFogState ReadEngineFog()
    {
        return {
            FogCVars::Enabled.Get(),
            FogCVars::Color.Get(),
            FogCVars::ColorIntensity.Get(),
            FogCVars::Density.Get(),
            FogCVars::HeightFalloff.Get(),
            FogCVars::HeightRefM.Get(),
            FogCVars::StartDistanceM.Get(),
            FogCVars::MaxOpacity.Get(),
            FogCVars::ApplyToSky.Get(),
            FogCVars::SkyColorBlend.Get(),
            FogCVars::SunTint.Get(),
            FogCVars::SunScatteringGain.Get(),
            FogCVars::SunScatteringExponent.Get(),
        };
    }

    /// @brief r.Fog.* へ書き戻す
    /// @note CVar::Set は値が実際に変わったときだけ通知するので、毎フレーム呼んでよい
    void WriteEngineFog(const EngineFogState& state)
    {
        FogCVars::Enabled.Set(state.enabled);
        FogCVars::Color.Set(state.color);
        FogCVars::ColorIntensity.Set(state.colorIntensity);
        FogCVars::Density.Set(state.density);
        FogCVars::HeightFalloff.Set(state.heightFalloff);
        FogCVars::HeightRefM.Set(state.heightRef);
        FogCVars::StartDistanceM.Set(state.startDistance);
        FogCVars::MaxOpacity.Set(state.maxOpacity);
        FogCVars::ApplyToSky.Set(state.applyToSky);
        FogCVars::SkyColorBlend.Set(state.skyColorBlend);
        FogCVars::SunTint.Set(state.sunTint);
        FogCVars::SunScatteringGain.Set(state.sunGain);
        FogCVars::SunScatteringExponent.Set(state.sunExponent);
    }

    /// 突入演出のあいだだけ雲を持ち上げるための一時値。
    /// CVar ではないので保存されない（保存されると次回起動のタイトルが雲の中で始まる）
    struct CloudLift {
        float amount = 0.0f;       ///< 0 = CVar のとおり ／ 1 = 下の 2 つ
        float baseHeight = 0.0f;
        float heightFalloff = 1.0f;
    };
    CloudLift g_cloudLift{};

    float MixToLift(float normal, float lifted, float amount)
    {
        return normal + (lifted - normal) * amount;
    }

    /// @brief 調整値から、このシーンで使う r.Fog.* を組み立てる
    /// @param nightBrightnessScale 夜の落とし込み倍率（1 = 昼。ComputeNightBrightnessScale）
    EngineFogState BuildGameFog(float nightBrightnessScale)
    {
        EngineFogState state{};
        state.enabled = true;
        state.color = cvColor.Get();
        // 色ではなく明るさ側を落とす。色を暗くすると Color の色味そのものが
        // 分からなくなり、インスペクターで昼の色を決められなくなる
        state.colorIntensity = cvBrightness.Get() * nightBrightnessScale;
        state.density = cvDensity.Get();
        // 突入演出のあいだだけ、雲の高さと柔らかさを一時値へ寄せる
        state.heightFalloff = MixToLift(
            cvHeightFalloff.Get(), g_cloudLift.heightFalloff, g_cloudLift.amount);
        state.heightRef = MixToLift(
            cvBaseHeight.Get(), g_cloudLift.baseHeight, g_cloudLift.amount);
        state.startDistance = cvStartDistance.Get();
        state.maxOpacity = cvMaxOpacity.Get();
        // ステージの外（描画物が無いピクセル）にも掛ける。ここが雲の本体で、
        // 切るとステージの真下だけしか雲にならない
        state.applyToSky = true;
        state.skyColorBlend = cvSkyColorBlend.Get();
        state.sunTint = cvSunTint.Get();
        state.sunGain = cvSunGain.Get();
        state.sunExponent = cvSunExponent.Get();
        return state;
    }

    // ──────────────────────────────────────────────────────────
    // Feature
    // ──────────────────────────────────────────────────────────

    /// @brief 登録したシーン（ゲーム・リザルト）の間だけ、ステージより下を雲で埋める Feature
    /// @details フォグ設定はエンジン寿命の CVar（r.Fog.*）なので、シーン開始時に現在値を
    ///          退避し、終了時に書き戻す。タイトルへ持ち出さないため。
    /// @note シーン中は毎フレーム Game.Fog.* を r.Fog.* へ流し込む。エディタの
    ///       「Height Fog」から r.Fog.* を直接いじっても次のフレームで戻るので、
    ///       このシーンの見た目は「ゲーム設定」の Game.Fog.* だけで決まる。
    /// @note オン/オフは 2 系統ある。シーン側の SetEnabled()（開発者の指定）と
    ///       CVar Game.Fog.Enabled（「ゲーム設定」からの全体スイッチ）の AND。
    ///       どちらで切っても、書き戻す先はシーン開始時点の r.Fog.* で同じ。
    class SkyFogFeature final : public GameComponents::ISkyFogFeature {
    public:
        explicit SkyFogFeature(bool enabled) : sceneEnabled_(enabled) {}

        const char* GetName() const override { return "GameSkyFog"; }

        /// @brief シーン側のオン/オフ（CVar Game.Fog.Enabled との AND で決まる）
        void SetEnabled(bool enabled) override
        {
            if (sceneEnabled_ == enabled) {
                return;
            }
            sceneEnabled_ = enabled;
            // Initialize 前は savedFog_ が空なので書き戻してはいけない
            // （初期化時の Sync() がこの値を見て反映する）
            if (initialized_) {
                Sync();
            }
        }

        bool IsEnabled() const override { return sceneEnabled_; }

        void Initialize(SceneContext& ctx) override
        {
            // 前のシーンの演出値を持ち越さない
            g_cloudLift = {};
            savedFog_ = ReadEngineFog();
            initialized_ = true;
            RefreshNightBrightnessScale(ctx);
            Sync();
        }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override
        {
            // フォグ設定を読むのは EnvironmentFeature（PostLogic）なので、
            // それより前のフェーズで流し込む。
            // 太陽を動かす TimeOfDayFeature も同じ FrameStart だが、GameScene が
            // 先に登録しているので、ここで読む高度はこのフレームの値になる
            if (phase == SceneUpdatePhase::FrameStart) {
                RefreshNightBrightnessScale(ctx);
                Sync();
            }
        }

        /// @brief 停止中も回す（止めると「ゲーム設定」で値を変えても画面が変わらない）
        bool RunsWhileStopped() const override { return true; }

        void Finalize(SceneContext&) override
        {
            g_cloudLift = {};
            WriteEngineFog(savedFog_);
        }

    private:
        /// @brief 太陽高度から夜の落とし込み倍率を求め直す
        /// @note ライトが引けないフレームは直前の倍率を保つ。1 へ戻すと、
        ///       シーン遷移などで一瞬だけ夜に雲が白く光ることになる
        void RefreshNightBrightnessScale(SceneContext& ctx)
        {
            auto* lightManager = ctx.engine
                ? ctx.engine->GetService<LightManager>() : nullptr;
            if (!lightManager) {
                return;
            }
            nightBrightnessScale_ =
                ComputeNightBrightnessScale(
                    ComputeNightFactor(ComputeSunElevationDeg(*lightManager)));
        }

        /// @brief 調整値を r.Fog.* へ反映する（無効なら退避した値へ戻す）
        /// @note シーン側（sceneEnabled_）と「ゲーム設定」の CVar は AND。
        ///       どちらか一方でも切れば雲は出ない
        void Sync() const
        {
            const bool show = sceneEnabled_ && cvEnabled.Get();
            WriteEngineFog(show ? BuildGameFog(nightBrightnessScale_) : savedFog_);
        }

        /// シーン開始時点の r.Fog.*（シーン終了時にここへ戻す）
        EngineFogState savedFog_{};

        /// 直近の夜の落とし込み倍率（1 = 昼）
        float nightBrightnessScale_ = 1.0f;

        /// シーン側のオン/オフ（CVar Game.Fog.Enabled とは独立）
        bool sceneEnabled_ = true;

        /// Initialize 済みか（savedFog_ が有効かの判定に使う）
        bool initialized_ = false;
    };
}

std::unique_ptr<GameComponents::ISkyFogFeature>
GameComponents::CreateSkyFogFeature(bool enabled)
{
    return std::make_unique<SkyFogFeature>(enabled);
}

void GameComponents::SetSkyFogCloudLift(float lift, float baseHeight, float heightFalloff)
{
    g_cloudLift.amount = std::clamp(lift, 0.0f, 1.0f);
    g_cloudLift.baseHeight = baseHeight;
    g_cloudLift.heightFalloff = std::max(0.01f, heightFalloff);
}
