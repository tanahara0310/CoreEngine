#pragma once

#ifdef CORE_EDITOR

#include "Graphics/Light/Light.h"
#include "Math/Vector/Vector3.h"

#include <optional>

namespace CoreEngine
{
    class AtmosphereManager;
    class EngineSystem;
    class GameObject;
    class LightComponent;
    class LightManager;
    class TimeOfDayFeature;
    class ToneMapping;

    /// @brief 空の上の天体
    enum class SkyBody
    {
        Sun,
        Moon,
    };

    /// @brief 空の上の位置
    struct SkyAngles
    {
        float elevationDeg = 30.0f; ///< 高度 [deg]（0 で地平線・90 で天頂・負で地平線の下）
        float azimuthDeg = 0.0f;    ///< 方位 [deg]（0 で北＝+Z・90 で東＝+X。-180〜180）
    };

    /// @brief 大気の太陽のライトの値
    struct SunSettings
    {
        SkyAngles angles{ 30.0f, 0.0f };
        float skyIntensity = 20.0f;         ///< 空の明るさ（大気散乱の輝度の倍率）
        float illuminanceLux = 100000.0f;   ///< 地表を照らす照度 [lx]
        Vector3 color{ 1.0f, 1.0f, 1.0f };  ///< 光の色
    };

    /// @brief 大気の月のライトの値
    struct MoonSettings
    {
        bool enabled = false;                  ///< 月を出すか
        SkyAngles angles{ 30.0f, 180.0f };
        float skyIntensity = 0.02f;            ///< 空の明るさ（大気散乱の輝度の倍率）
        float illuminanceLux = 114.0f;         ///< 地表を照らす照度 [lx]
        Vector3 color{ 0.55f, 0.65f, 0.85f };  ///< 光の色
    };

    /// @brief シーンビューのカメラの向きと画角
    struct SkyViewCamera
    {
        Vector3 forward{ 0.0f, 0.0f, 1.0f };
        Vector3 right{ 1.0f, 0.0f, 0.0f };
        Vector3 up{ 0.0f, 1.0f, 0.0f };
        float tanHalfFovX = 1.0f;     ///< 横の画角の半分の tan
        float tanHalfFovY = 0.5625f;  ///< 縦の画角の半分の tan
    };

    /// @brief 方位を 8 方位の名前にする（北・北東・東 …）
    const char* CompassName(float azimuthDeg);

    /// @brief 方位を -180〜180 へ巻き取る
    float WrapAzimuth(float azimuthDeg);

    /// @brief 画面に出す方位（0〜360、小数 1 桁で 360.0 にならない値）
    float AzimuthForDisplay(float azimuthDeg);

    /// @brief 地表から天体を見る向き（長さ 1）
    Vector3 SkyAnglesToDirection(const SkyAngles& angles);

    /// @brief 地表から天体を見る向きを空の上の位置にする
    /// @param fallbackAzimuthDeg 真上・真下で方位が決まらないときの方位
    SkyAngles DirectionToSkyAngles(const Vector3& toBody, float fallbackAzimuthDeg);

    /// @brief 2 つの位置の間の角度 [deg]
    float AngleBetween(const SkyAngles& a, const SkyAngles& b);

    /// @brief 大気の太陽と月のライトを、空の上の位置と時刻で読み書きする
    /// @details 描く前に SyncFromLights で今のシーンのライトから読み直す。
    ///          シーンに時刻の機能（TimeOfDay）があるときは、太陽の位置と時刻をその機能へ渡す。
    class SunMoonController
    {
    public:
        /// 空の図と欄で扱う高度の下限 [deg]
        static constexpr float kMinElevationDeg = -20.0f;

        /// @brief 太陽と月のライトの値の控え
        struct LightSnapshot
        {
            LightComponent* sun = nullptr;
            LightComponent* moon = nullptr;
            Light sunValues{};
            Light moonValues{};
            bool sunEnabled = false;
            bool moonEnabled = false;
        };

        void Initialize(EngineSystem& engine);

        /// @brief 今の太陽と月のライトから値を読み直す
        void SyncFromLights();

        const SunSettings& GetSun() const { return sun_; }
        const MoonSettings& GetMoon() const { return moon_; }

        /// @brief 大気の太陽のライトがあるか
        bool HasSun() const;

        /// @brief 太陽の値をライトへ書く
        void ApplySun(const SunSettings& settings);

        /// @brief 月の値をライトへ書く（月のライトが無ければ、出すときにオブジェクトごと作る）
        void ApplyMoon(const MoonSettings& settings);

        /// @brief 天体の位置
        SkyAngles GetAngles(SkyBody body) const;

        /// @brief 天体が空に出ているか（太陽はライトがあるか・月は出しているか）
        bool IsShown(SkyBody body) const;

        /// @brief 天体を手で動かせるか（空に出ていれば動かせる）
        bool CanMove(SkyBody body) const;

        /// @brief 天体をその位置へ動かす
        /// @note 時刻の機能があるときは軌道の上でいちばん近い所へ動かす（太陽は時刻を、月は月齢を変える）。
        void MoveTo(SkyBody body, const SkyAngles& angles);

        // ===== 時刻 =====

        /// @brief 今のシーンの時刻の機能（無ければ nullptr）
        TimeOfDayFeature* FindTimeOfDay() const;

        /// @brief 時刻の機能が太陽と月を動かしているか
        bool IsTimeDriven() const { return FindTimeOfDay() != nullptr; }

        /// @brief 今の時刻 [h]
        float GetTimeOfDay() const;

        /// @brief 時刻を変える（時刻の機能が無ければ、その時刻の位置へ太陽を置く）
        void SetTimeOfDay(float hours);

        /// @brief 太陽の軌道を決める緯度 [deg]
        float GetLatitudeDeg() const;

        /// @brief 緯度を変える（時刻の機能が無いときだけ。太陽を今の時刻の位置へ置き直す）
        void SetLatitudeDeg(float latitudeDeg);

        /// @brief 軌道を方位の向きへ回す量 [deg]
        float GetOrbitOffsetDeg() const;

        /// @brief 軌道の向きを変える（時刻の機能が無いときだけ。太陽を今の時刻の位置へ置き直す）
        void SetOrbitOffsetDeg(float offsetDeg);

        /// @brief その時刻の太陽の軌道の上の位置（高度は下限で切らない）
        SkyAngles SunAnglesAt(float hours) const;

        /// @brief 太陽が今の時刻の位置にあるか（手で動かすと外れる）
        bool IsSunOnTimePath() const;

        /// @brief 月齢 [日]（時刻の機能があるときだけ。無ければ -1）
        float GetMoonAge() const;

        /// @brief 月齢を変える（時刻の機能があるときだけ）
        void SetMoonAge(float days);

        /// @brief 時刻を自動で進めるか（時刻の機能が無いときに使う）
        bool IsAutoAdvancing() const { return autoAdvance_; }
        void SetAutoAdvancing(bool advancing) { autoAdvance_ = advancing; }

        /// @brief 自動で進める速さ [h/s]
        float GetAdvanceSpeed() const { return advanceHoursPerSecond_; }
        void SetAdvanceSpeed(float hoursPerSecond) { advanceHoursPerSecond_ = hoursPerSecond; }

        /// @brief 自動で進めるときは時刻を進める（同じフレームに何度呼んでも 1 回だけ進める）
        void Tick(float deltaSeconds);

        // ===== まとめて置く =====

        /// @brief 昼の時刻へ太陽を置く（夜のプリセットが入れた自動露出は元へ戻す）
        void ApplyDaytimePreset(float hour);

        /// @brief 夜にして満月を出し、自動露出を入れる
        void ApplyNightPreset();

        /// @brief 月を太陽のちょうど反対（満月の位置）へ置く（時刻の機能があるときは月齢を満月にする）
        void PlaceMoonOppositeSun();

        /// @brief 天体をシーンビューのカメラの正面へ置く
        /// @return カメラの向きが分からず置けなければ false
        bool PlaceAtView(SkyBody body);

        // ===== 履歴 =====

        /// @brief 操作を始める前の値を控える（控えがあればそのまま）
        void BeginEdit();

        /// @brief 控えた値から変わっていれば、1 件だけ履歴へ積む
        void EndEdit();

        /// @brief ImGui の直前の項目を掴んだら控え、離したら CommitTrackedEdit で積む印を付ける
        void TrackItem();

        /// @brief 離した印があれば EndEdit する（パネルを描き終えたときに呼ぶ）
        void CommitTrackedEdit();

        // ===== シーンビューのカメラ =====

        /// @brief シーンビューを描いたフレームのカメラを覚える
        void SetViewCamera(const SkyViewCamera& camera);

        /// @brief 直近のフレームのシーンビューのカメラ（描いていなければ nullptr）
        const SkyViewCamera* GetViewCamera() const;

        // ===== オブジェクト =====

        /// @brief 天体のライト（無ければ nullptr）
        LightComponent* FindLight(SkyBody body) const;

        /// @brief 太陽か月のライトを持つオブジェクトか
        bool IsSkyLightObject(const GameObject* object) const;

        /// @brief 天体のライトを持つオブジェクトを選ぶ
        void SelectLightObject(SkyBody body) const;

        AtmosphereManager* GetAtmosphereManager() const;

    private:
        /// @brief 時刻の機能の始まりの時刻と月齢（シーンに保存される値）
        struct StartValues
        {
            float hour = 0.0f;
            float moonAgeDays = 0.0f;
        };

        LightSnapshot Capture() const;
        LightManager* GetLightManager() const;
        ToneMapping* GetToneMapping() const;

        /// @brief 時刻の機能があり、停止中か（始まりの時刻と月齢へも書くか）
        bool WritesStartValues() const;

        /// @brief 時刻の機能の始まりの時刻と月齢を読む
        StartValues ReadStartValues() const;

        /// @brief 今の時刻の位置へ太陽を置く（時刻の機能が無いとき）
        void ApplyLocalTime();

        /// @brief 軌道の上でその位置にいちばん近い時刻 [h]
        float NearestTimeOnPath(const SkyAngles& angles) const;

        /// @brief 夜のプリセットが入れた自動露出を、入れる前の値へ戻す
        void RestoreAutoExposure();

        EngineSystem* engine_ = nullptr;
        SunSettings sun_{};
        MoonSettings moon_{};

        float timeOfDay_ = 12.0f;               ///< 時刻 [h]（時刻の機能が無いとき）
        float latitudeDeg_ = 35.0f;             ///< 緯度 [deg]（時刻の機能が無いとき）
        float orbitOffsetDeg_ = 0.0f;           ///< 軌道の向き [deg]（時刻の機能が無いとき）
        bool autoAdvance_ = false;
        float advanceHoursPerSecond_ = 0.5f;
        int lastTickFrame_ = -1;                ///< 時刻を進めたフレーム

        std::optional<LightSnapshot> editBefore_; ///< 操作を始める前の値
        std::optional<StartValues> startBefore_;  ///< 操作を始める前の始まりの時刻と月齢（停止中だけ控える）
        bool editEnded_ = false;                  ///< 掴んでいた項目を離した

        SkyViewCamera viewCamera_{};
        int viewCameraFrame_ = -1000;             ///< カメラを覚えたフレーム

        bool autoExposureSaved_ = false;       ///< 夜のプリセットの前の自動露出を控えているか
        bool autoExposureBeforeNight_ = false; ///< 夜のプリセットの前の自動露出
    };
}

#endif // CORE_EDITOR
