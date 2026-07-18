#pragma once

#include "Math/MathCore.h"

namespace CoreEngine {
    class EngineSystem;
    class AtmosphereManager;
    class LightManager;
}

/// @brief Atmosphere UI から扱う太陽設定の読み書きモデル
struct AtmosphereEditorSunSettings {
    float elevationDeg = 30.0f; ///< 太陽高度角 [deg]（0=地平線, 90=天頂, 負値=地平線下）
    float azimuthDeg = 0.0f;    ///< 太陽方位角 [deg]（0=+Z方向, 時計回り）
    float intensity = 20.0f;    ///< 太陽光強度（大気散乱の輝度スケール。空の明るさに直結）
};

/// @brief Atmosphere UI から扱う月（第2大気ライト）設定の読み書きモデル
/// @details 空の強度は太陽(20)の1/1000（美術値。物理比 1/40万では見えない）。
///          直接光は太陽の「空:直接光」比（20:1.75 ≈ 0.0875）を月の空強度へ適用した
///          0.00175 を丸めた値。夜の視認性は自動露出（夜プリセットが有効化）が担い、
///          直接光を人為的に盛らないことで「空だけ暗く床だけ明るい」露出の破綻を防ぐ。
struct AtmosphereEditorMoonSettings {
    bool enabled = false;            ///< 月ライトの有効/無効（オプトイン）
    float elevationDeg = 30.0f;      ///< 月高度角 [deg]
    float azimuthDeg = 180.0f;       ///< 月方位角 [deg]（既定は太陽の反対側）
    float skyIntensity = 0.02f;      ///< 月光強度（大気散乱の輝度スケール）
    float surfaceIntensity = 0.002f; ///< 月光強度（サーフェス直接光。太陽の kAtmosphereSurfaceSunIntensity=1.75 と同じ単位）
    CoreEngine::Vector3 color = { 0.55f, 0.65f, 0.85f }; ///< 月光色（知覚的な青白さの美術値）
};

/// @brief Atmosphere UI と Engine 内部 AtmosphereManager / LightManager の仲介を担当する facade
/// @details UI はこの facade だけを通して大気散乱関連の設定を取得・適用する。
///          Initialize で GameDebugUI の環境エディタ（Hierarchy の Environment ツリー）へ
///          自己登録し、選択時に Inspector 内へ編集パネルを描画する。
///          （VolumetricCloudTestScene の CloudEditorFacade と同じ役割分担）
class AtmosphereEditorFacade {
public:
    /// @brief 環境エディタの登録を解除する
    ~AtmosphereEditorFacade();

    /// @brief facade の参照先を初期化し、環境エディタとして登録する
    void Initialize(CoreEngine::EngineSystem& engine);

    /// @brief 現在の太陽設定を取得する
    AtmosphereEditorSunSettings GetSunSettings() const { return sunSettings_; }

    /// @brief 太陽設定を適用する（太陽ライトの方向・強度へ反映し、LUT再計算を要求）
    void ApplySunSettings(const AtmosphereEditorSunSettings& settings);

    /// @brief 現在の月設定を取得する
    AtmosphereEditorMoonSettings GetMoonSettings() const { return moonSettings_; }

    /// @brief 月設定を適用する（月ライトが無ければ有効化時に生成し、方向・色・強度へ反映）
    void ApplyMoonSettings(const AtmosphereEditorMoonSettings& settings);

    /// @brief 高度角・方位角から太陽光の進行方向ベクトルを計算する
    /// @return 正規化済みのライト方向（太陽から地表へ向かう方向）
    static CoreEngine::Vector3 ComputeSunLightDirection(float elevationDeg, float azimuthDeg);

private:
    /// @brief 大気散乱の編集パネル内容を描画する（Inspector 内に埋め込み）
    void DrawContent();

    /// @brief 太陽・月をドラッグで配置するスカイマップ（極座標ドーム）ウィジェットを描画する
    /// @details 円の中心=天頂（高度90°）・円周=地平線（0°）・外周リング=地平線下（-20°まで）。
    ///          UE のビューポート太陽ドラッグ（Ctrl+L）相当の直接操作を Inspector 内で行う。
    void DrawSunMoonPlacementWidget();

    /// @brief 時刻・緯度から太陽の高度角・方位角を計算して適用する（春分・赤緯0の太陽軌道）
    void ApplyTimeOfDay();

    CoreEngine::AtmosphereManager* GetAtmosphereManager() const;
    CoreEngine::LightManager* GetLightManager() const;

    AtmosphereEditorSunSettings sunSettings_{};
    AtmosphereEditorMoonSettings moonSettings_{};
    CoreEngine::EngineSystem* engine_ = nullptr;

    // 時刻ベースの太陽配置（UE の Sun Position 相当の簡易版）
    float timeOfDay_ = 12.0f;            ///< 時刻 [h]（0-24）
    float latitudeDeg_ = 35.0f;          ///< 観測地の緯度 [deg]（既定は東京相当）
    bool autoTimeCycle_ = false;         ///< 時刻を自動で進める（昼夜サイクル確認用）
    float timeSpeedHoursPerSec_ = 0.5f;  ///< 自動進行の速度 [h/s]

    int placementDrag_ = 0;              ///< スカイマップのドラッグ対象（0=なし 1=太陽 2=月）
};
