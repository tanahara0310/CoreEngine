#pragma once

#include "ISceneFeature.h"
#include "Graphics/Light/Light.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    class EngineSystem;
    class ICVar;
    class LightManager;
    class ToneMapping;

    /// @brief 時刻を進めて空と光を昼→夕→夜と変化させる Feature（昼夜サイクル）
    /// @details 太陽・月の「向き」だけを毎フレーム書き換える。空の色・薄明・地表の減光は
    ///          すべて AtmosphereManager（大気散乱の LUT と Transmittance on Light）が
    ///          向きから導くため、この Feature は明るさを人為的に触らない。
    /// @note 追加するだけで成立する（`AddFeature(std::make_unique<TimeOfDayFeature>())`）。
    ///       ライトの GPU 転送（LightingFeature::UpdateAll）は同じ FrameStart の
    ///       先に走るため、平行光の反映は空より 1 フレーム遅れる。サイクル 1 周が
    ///       分単位である限り太陽は 1 フレームで 0.03° も動かず、知覚できない。
    /// @note 夜の暗さは「月光 [lx]・自動露出の上限 EV・空アンビエント」の 3 つで決まる。
    ///       後ろ 2 つは他機能の CVar（r.AutoExposure.MaxEV / r.Atmosphere.SkyAmbientScale）
    ///       なので、サイクルの間だけ名前で借りて上書きする。露出上限は夜だけが張り付く
    ///       値なので昼には影響せず、空アンビエントは夜の度合いで倍率を掛ける。
    /// @note シーン終了時に太陽・月・自動露出・借りた CVar をサイクル開始前の状態へ戻す。
    ///       戻さないと EnvironmentFeature が終了時に写す CVar
    ///       （r.AtmosphereLights.*）や保存 JSON へ夜向けの値が焼き付き、次のシーンや
    ///       アプリ次回起動が夜のまま始まる。
    class TimeOfDayFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "TimeOfDay"; }

        /// @brief 開始時刻を CVar から取り込み、設定パネルを登録する
        void Initialize(SceneContext& ctx) override;

        /// @brief 時刻を進め、太陽・月・自動露出へ反映する（FrameStart）
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 太陽・月・自動露出をサイクル開始前の状態へ戻す
        void Finalize(SceneContext& ctx) override;

        /// @brief 停止中も回す（止めると時刻スライダーを動かしても空が変わらない）
        /// @note 自動進行は Time::DeltaTime() 基準なので、停止中は進まず編集だけが効く。
        bool RunsWhileStopped() const override { return true; }

        // ==================== 時刻 ====================

        /// @brief 時刻を直接設定する [h]（0-24 の範囲へ巻き取る）
        /// @note 進行距離やゲーム進行に連動させたい場合は、自動進行を切ってこれを毎フレーム呼ぶ。
        void SetTimeOfDay(float hours);

        /// @brief 現在の時刻 [h]（0-24）
        float GetTimeOfDay() const { return timeOfDay_; }

        /// @brief 時刻の自動進行を切り替える
        void SetAdvancing(bool advancing);
        bool IsAdvancing() const;

        /// @brief ゲーム内 24 時間を何秒で回すかを設定する [s]
        void SetDayLengthSeconds(float seconds);
        float GetDayLengthSeconds() const;

        // ==================== 現在の状態 ====================

        /// @brief 太陽の高度角 [deg]（0 = 地平線 / 90 = 天頂 / 負値 = 地平線下）
        float GetSunElevationDeg() const { return sunElevationDeg_; }

        /// @brief 太陽の方位角 [deg]（0 = +Z 方向）
        float GetSunAzimuthDeg() const { return sunAzimuthDeg_; }

        /// @brief 太陽が地平線より下か
        bool IsNight() const { return sunElevationDeg_ < 0.0f; }

        /// @brief 夜の度合い（0 = 昼 / 1 = 薄明を抜けた夜）
        /// @details 太陽高度 0° から r.TimeOfDay.TwilightDeg（既定 6° = 市民薄明）まで
        ///          滑らかに 1 へ向かう。街灯やかがり火のフェードイン係数に使える。
        float GetNightFactor() const { return nightFactor_; }

        /// @brief 時刻と緯度から太陽の高度角・方位角を求める（春分・赤緯 0 の軌道）
        static void ComputeSunAngles(float hours, float latitudeDeg,
            float& outElevationDeg, float& outAzimuthDeg);

        /// @brief 高度角・方位角から光の進行方向（光源 → 地表）を求める
        static Vector3 ComputeLightDirection(float elevationDeg, float azimuthDeg);

#ifdef USE_IMGUI
        /// @brief Engine Settings に「Time of Day」パネルを登録する（プロセスで一度だけ）
        static void EnsureSettingsPanelRegistered(EngineSystem* engine);

        /// @brief このサイクルをパネルの編集対象にする（nullptr で解除）
        static void SetActiveForSettingsPanel(TimeOfDayFeature* feature);

        /// @brief 設定パネルの中身を描画する
        void DrawSettingsImGui();
#endif

    private:
        /// @brief 他機能が持つ float CVar を、サイクルの間だけ借りるための状態
        /// @details 公開 API の無いファイルスコープ CVar（r.AutoExposure.MaxEV など）を
        ///          名前で引いて上書きし、Finalize で元の値へ必ず返す。
        struct BorrowedCVar {
            ICVar* cvar = nullptr;
            float original = 0.0f;
            bool held = false;
        };

        /// @brief 現在の時刻を太陽・月ライトへ書き込む（初回に元の状態を退避する）
        void ApplyToLights(SceneContext& ctx);

        /// @brief 自動露出の強制 ON / 解除を CVar に合わせる
        void ApplyAutoExposure(SceneContext& ctx);

        /// @brief 夜の暗さを決める借り物 CVar（露出上限・空アンビエント）を更新する
        void ApplyNightDarkness();

        /// @brief 借りている CVar をすべて元の値へ返す
        void ReleaseBorrowedCVars();

        /// @brief 名前で float CVar を引いて借りる（初回だけ元の値を控える）
        static void BorrowFloatCVar(BorrowedCVar& slot, const char* name);

        /// @brief 借りている CVar へ書く（借りられていなければ何もしない）
        static void WriteFloatCVar(BorrowedCVar& slot, float value);

        /// @brief 借りている CVar を元の値へ返す
        static void ReleaseFloatCVar(BorrowedCVar& slot);

        static LightManager* GetLightManager(SceneContext& ctx);
        static ToneMapping* GetToneMapping(SceneContext& ctx);

        float timeOfDay_ = 12.0f;         ///< 現在時刻 [h]（0-24）
        float sunElevationDeg_ = 90.0f;   ///< 直近に適用した太陽高度角 [deg]
        float sunAzimuthDeg_ = 0.0f;      ///< 直近に適用した太陽方位角 [deg]
        float nightFactor_ = 0.0f;        ///< 夜の度合い（0-1）

        // ---- サイクル開始前の状態（Finalize で戻す）----
        // 退避は初回の ApplyToLights で行う。Initialize の時点では
        // EnvironmentFeature の CVar 復元（PostSceneInitialize）がまだ走っておらず、
        // 「戻すべき状態」が確定していないため。
        bool savedSunValid_ = false;
        Vector3 savedSunDirection_{ 0.0f, -1.0f, 0.0f };

        LightHandle createdMoon_{};       ///< この Feature が生成した月（借り物なら無効ハンドル）
        bool savedMoonValid_ = false;
        bool savedMoonEnabled_ = false;
        Vector3 savedMoonDirection_{ 0.0f, -1.0f, 0.0f };
        Vector3 savedMoonColor_{ 1.0f, 1.0f, 1.0f };
        float savedMoonIntensity_ = 0.0f;
        float savedMoonSkyIntensity_ = 0.0f;

        bool autoExposureOverridden_ = false; ///< 自動露出を強制 ON にしているか
        bool autoExposureBefore_ = false;     ///< 強制 ON にする前の値
        float currentAutoEV_ = 0.0f;          ///< 直近の自動露出 EV（パネル表示用）

        // ---- 夜の暗さのために借りている他機能の CVar（Finalize で返す）----
        BorrowedCVar maxAutoEV_;        ///< r.AutoExposure.MaxEV
        BorrowedCVar skyAmbientScale_;  ///< r.Atmosphere.SkyAmbientScale
    };
}
