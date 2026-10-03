#pragma once

#include "Graphics/Light/Light.h"
#include "Math/MathCore.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <optional>

namespace CoreEngine {
    class EngineSystem;
    class AtmosphereManager;
    class LightComponent;
    class LightManager;
    class ToneMapping;

    /// @brief Atmosphere UI から扱う太陽設定の読み書きモデル
    struct AtmosphereEditorSunSettings {
        float elevationDeg = 30.0f; ///< 太陽高度角 [deg]（0=地平線, 90=天頂, 負値=地平線下）
        float azimuthDeg = 0.0f;    ///< 太陽方位角 [deg]（0=+Z方向, 時計回り）
        float intensity = 20.0f;    ///< 太陽光強度（大気散乱の輝度スケール。空の明るさに直結）
    };

    /// @brief Atmosphere UI から扱う月（第2大気ライト）設定の読み書きモデル
    /// @note 強度はいずれも物理値ではなく美術値（物理満月 0.25lx では見えないため）。
    ///       夜の視認性は自動露出が担い、直接光は人為的に盛らない。
    struct AtmosphereEditorMoonSettings {
        bool enabled = false;            ///< 月ライトの有効/無効（オプトイン）
        float elevationDeg = 30.0f;      ///< 月高度角 [deg]
        float azimuthDeg = 180.0f;       ///< 月方位角 [deg]（既定は太陽の反対側）
        float skyIntensity = 0.02f;      ///< 月光強度（大気散乱の輝度スケール）
        float surfaceIntensity = 114.0f; ///< 月光強度（サーフェス直接光の照度 [lx]）
        Vector3 color = { 0.55f, 0.65f, 0.85f }; ///< 月光色（知覚的な青白さの美術値）
    };

    /// @brief 大気の太陽と月のライトの値（Sky Atmosphere パネルの Undo の控え）
    struct AtmosphereLightState {
        LightComponent* sun = nullptr;  ///< 太陽のライト（無ければ nullptr）
        LightComponent* moon = nullptr; ///< 月のライト（無ければ nullptr）
        Light sunValues{};
        Light moonValues{};
        bool sunEnabled = false;
        bool moonEnabled = false;
    };

    /// @brief 大気散乱（Sky Atmosphere）のエンジン常駐エディタ
    /// @details DebugSubsystem がエンジン寿命で 1 個所有し、シーンに置かれたコンポーネントの
    ///          インスペクタとして中身を描く。
    /// @note 編集対象のライトはシーンごとに作り直されるため、描画のたびに UI モデルを再同期する。
    class AtmosphereEditor {
    public:
        /// @brief 参照先を初期化し、空のコンポーネントのインスペクタとして登録する
        void Initialize(EngineSystem& engine);

        /// @brief 現在の太陽設定を取得する
        AtmosphereEditorSunSettings GetSunSettings() const { return sunSettings_; }

        /// @brief 太陽設定を適用する（太陽ライトの方向・強度へ反映し、LUT再計算を要求）
        void ApplySunSettings(const AtmosphereEditorSunSettings& settings);

        /// @brief 現在の月設定を取得する
        AtmosphereEditorMoonSettings GetMoonSettings() const { return moonSettings_; }

        /// @brief 月設定を適用する（月ライトが無ければ有効化時に生成し、方向・色・強度へ反映）
        void ApplyMoonSettings(const AtmosphereEditorMoonSettings& settings);

    private:
        /// @brief 大気散乱の編集パネル内容を描画する（Inspector 内に埋め込み）
        void DrawContent();

        /// @brief 現在の太陽・月ライトから UI モデル（高度角・方位角・強度）を再同期する
        /// @details エディタはエンジン寿命・ライトはシーン寿命のため、シーン切替や
        ///          シーン側の初期化で UI キャッシュが古くなる。描画のたびに実体から
        ///          読み戻すことで、常に「今のシーンの状態」を表示・編集の起点にする。
        void SyncFromLights();

        /// @brief 太陽・月をドラッグで配置するスカイマップ（極座標ドーム）ウィジェットを描画する
        /// @details 円の中心=天頂（高度90°）・円周=地平線（0°）・外周リング=地平線下（-20°まで）。
        ///          UE のビューポート太陽ドラッグ（Ctrl+L）相当の直接操作を Inspector 内で行う。
        void DrawSunMoonPlacementWidget();

        /// @brief 時刻・緯度から太陽の高度角・方位角を計算して適用する（春分・赤緯0の太陽軌道）
        void ApplyTimeOfDay();

        /// @brief 昼系プリセット（正午・朝・夕暮れ）を適用する
        /// @details 時刻の適用に加え、夜プリセットが変更した自動露出を元へ戻す。
        ///          プリセット同士を対称にしておかないと「夜を経由すると昼が暗いまま」になる。
        void ApplyDaytimePreset(float hour);

        /// @brief 夜（満月）プリセットを適用する（時刻0時・月の配置・自動露出の有効化）
        void ApplyNightPreset();

        /// @brief 夜プリセットが変更した自動露出の設定を、プリセット適用前の値へ戻す
        /// @details 夜プリセット後にユーザーが自分で自動露出を切っていた場合は、
        ///          その操作を上書きしないよう復元をスキップする。
        void RestoreAutoExposureFromNightPreset();

        /// @brief 今の太陽と月のライトの値を控える
        AtmosphereLightState CaptureLightState() const;

        /// @brief 直前の項目を掴んだら値を控え、離したら DrawContent の最後に履歴へ積む
        void TrackLightEdit();

        /// @brief ボタンのように 1 回で終わる操作の前に値を控え、DrawContent の最後に履歴へ積む
        void BeginLightEdit();

        /// @brief 控えた値から変わっていれば、1 件だけ履歴へ積む（DrawContent の最後に呼ぶ）
        void CommitLightEdit();

        AtmosphereManager* GetAtmosphereManager() const;
        LightManager* GetLightManager() const;
        ToneMapping* GetToneMapping() const;

        /// @brief 大気の太陽のライト（無ければ nullptr）
        LightComponent* FindSunLight() const;

        /// @brief 大気の月のライト（無ければ nullptr）
        LightComponent* FindMoonLight() const;

        AtmosphereEditorSunSettings sunSettings_{};
        AtmosphereEditorMoonSettings moonSettings_{};
        EngineSystem* engine_ = nullptr;

        /// @brief インスペクタの出し方の登録（破棄すると外れる）
        ScopedRegistration inspector_;

        // 時刻ベースの太陽配置（UE の Sun Position 相当の簡易版）
        float timeOfDay_ = 12.0f;            ///< 時刻 [h]（0-24）
        float latitudeDeg_ = 35.0f;          ///< 観測地の緯度 [deg]（既定は東京相当）
        bool autoTimeCycle_ = false;         ///< 時刻を自動で進める（昼夜サイクル確認用）
        float timeSpeedHoursPerSec_ = 0.5f;  ///< 自動進行の速度 [h/s]

        int placementDrag_ = 0;              ///< スカイマップのドラッグ対象（0=なし 1=太陽 2=月）

        std::optional<AtmosphereLightState> lightEditBefore_; ///< 操作を始める前の太陽と月の値
        bool lightEditEnded_ = false;        ///< このフレームで操作を離した

        // 夜プリセットが自動露出を強制 ON にする前の値（昼系プリセットで復元するための退避）。
        // 復元は「夜 → 昼」の1往復ぶんだけ有効で、復元後は退避を破棄する
        bool autoExposureSaved_ = false;       ///< 退避値を保持しているか
        bool autoExposureBeforeNight_ = false; ///< 夜プリセット適用直前の自動露出の有効/無効
    };
}
