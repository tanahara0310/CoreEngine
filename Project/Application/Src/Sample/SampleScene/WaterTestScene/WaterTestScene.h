// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "Sample/TestGameObject/Effect/LightningStrikeObject.h"
#include "WaterReflectionPass.h"

#include <array>

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
#endif

class WaterTestScene : public CoreEngine::BaseScene {
public:

    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief ReflectionView の実行要求を返す
    /// @return 水面反射を要求する設定
    CoreEngine::ReflectionViewRequest GetReflectionViewRequest() const override;

    /// @brief ReflectionView 実行前に反射カメラ状態をセットアップする
    /// @param mainCamera 通常描画の基準カメラ
    /// @param planeHeight 反射平面の高さ
    void SetupReflectionView(CoreEngine::ICamera* mainCamera, float planeHeight) override;

    /// @brief ReflectionView 実行後に反射カメラ状態を元へ戻す
    /// @param mainCamera 復元対象の通常描画カメラ
    void RestoreReflectionView(CoreEngine::ICamera* mainCamera) override;

    /// @brief Engine 側で生成した ReflectionView 結果を水面へ適用する
    /// @param result ReflectionColor / SceneDepth / SceneColor / clip plane を含む結果
    void ApplyReflectionViewResult(const CoreEngine::ReflectionViewResult& result) override;

    /// @brief 解放
    void Finalize() override;

private:
    struct ActiveLightningImpact {
        CoreEngine::Vector3 position = { 0.0f, 0.0f, 0.0f };
        float elapsed = 0.0f;
        float ringRadius = 0.0f;
        float chargeRadius = 0.6f;
        float visualIntensity = 1.0f;
        bool active = false;
    };

#ifdef USE_IMGUI
    /// @brief 水面パラメータ ImGui パネルを描画する
    void DrawWaterImGui();

    /// @brief 雷エフェクト ImGui セクションを描画する
    void DrawLightningImGui();

    /// @brief 指定したプリセットを WaterPlaneObject と ImGui キャッシュに適用する
    void ApplyWaterPreset(WaterPresetType preset);

    /// @brief プリセット設定に基づいてレイヤー分けされた Gerstner Wave を再生成する
    void RegenerateLayeredWaves(WaterPresetType preset, uint32_t activeWaveCount);

    /// @brief 指定プリセットの推奨波本数をUIと水面へ反映する
    void RestoreRecommendedWaveCount(WaterPresetType preset);
#endif

    /// @brief ランダムな着弾位置で雷を発生させる
    void TriggerRandomLightningStrike();

    /// @brief 複数本の雷をランダム地点へ連続着弾させるバーストを開始する
    void StartLightningBurst();

    /// @brief バースト内の次の雷をランダム地点へ着弾させる
    void TriggerBurstStrike();

    /// @brief 指定位置へ雷を発生させる
    void TriggerLightningStrikeAt(const CoreEngine::Vector3& impactPosition);

    /// @brief 自動雷演出のタイマーを更新する
    void UpdateLightningSequence(float deltaTime);

    /// @brief 雷着弾後の水面演出状態を更新する
    void UpdateLightningImpactEffect(float deltaTime);

    /// @brief 水面グリッドメッシュオブジェクト
    WaterPlaneObject* waterPlane_ = nullptr;

    /// @brief 地面モデルオブジェクト
    ModelObject* groundObject_ = nullptr;

    /// @brief 雷エフェクトオブジェクト
    LightningStrikeObject* lightningStrike_ = nullptr;

    /// @brief 水面平面反射パス（Step 4）
    WaterReflectionPass reflectionPass_;

    bool  lightningEffectEnabled_ = true;
    bool  lightningAutoLoop_ = true;
    float lightningIntervalMin_ = 0.0f;
    float lightningIntervalMax_ = 0.06f;
    float lightningStrikeDuration_ = 0.55f;
    float lightningStrikeIntensity_ = 2.10f;
    float lightningTimer_ = 0.0f;
    int   lightningBurstRemaining_ = 0;
    float lightningBurstCooldown_ = 0.0f;
    float lightningStartHeightMin_ = 26.0f;
    float lightningStartHeightMax_ = 40.0f;
    float lightningStrikeRadius_ = 28.0f;
    float lightningImpactDuration_ = 1.6f;
    std::array<ActiveLightningImpact, kMaxWaterLightningImpactCount> lightningImpacts_{};
    uint32_t nextLightningImpactSlot_ = 0;

#ifdef USE_IMGUI
    // ---- ImGui 用キャッシュ（Material 側は Set/Get 経由で同期する） ----
    int   imguiPreset_     = static_cast<int>(WaterPresetType::Lake); ///< 現在選択中のプリセット
    float imguiColor_[4] = { 0.04f, 0.18f, 0.28f, 1.0f }; ///< 水面ベースカラー RGBA
    float imguiRoughness_ = 0.04f;
    float imguiMetallic_ = 0.0f;
    bool  imguiIBLEnabled_ = true;
    float imguiScrollSpeed_[2] = { 0.03f, 0.01f }; ///< UV スクロール速度
    float imguiUVTiling_[2] = { 4.0f, 4.0f };      ///< UV タイリング
    int   imguiActiveWaveCount_ = static_cast<int>(kMaxWaterWaveCount); ///< 現在有効な波本数
    bool  imguiLockRecommendedWaveCount_ = false;  ///< 推奨波本数へ固定する
    bool  imguiAutoRestoreRecommendedWaveCount_ = true; ///< プリセット切り替え時に推奨本数へ戻す
    bool  imguiAutoGenerateOnWaveCountIncrease_ = true; ///< 波本数を増やした際に自動再生成する
    float imguiFresnelReflectanceScale_ = 1.0f;     ///< Fresnel 反射ブレンドの強さ
    float imguiFresnelBaseReflectance_ = 0.02f;     ///< 正面入射時の反射率 F0

    // ---- Depth Fade ----
    bool  imguiDepthFadeEnabled_ = true;            ///< Depth Fade 有効フラグ
    float imguiAbsorptionCoeff_  = 1.2f;            ///< 光吸収係数
    float imguiShallowColor_[3]  = { 0.10f, 0.85f, 0.65f };  ///< 浅瀬の水色
    float imguiDeepColor_[3]     = { 0.02f, 0.08f, 0.45f }; ///< 深場の水色
    bool  imguiDepthFadeDebugEnabled_ = false;      ///< Depth Fade デバッグ表示
    float imguiDepthFadeDebugScale_ = 1.5f;         ///< Depth Fade デバッグ表示倍率
    int   imguiDepthDebugViewMode_ = static_cast<int>(WaterDebugViewMode::RawDepth); ///< 水面デバッグ可視化モード

    bool  imguiLightningAutoLoop_ = true;
    bool  imguiLightningEffectEnabled_ = true;
    float imguiLightningStrikeDuration_ = 0.55f;
    float imguiLightningStrikeIntensity_ = 2.10f;
    float imguiLightningIntervalMin_ = 0.0f;
    float imguiLightningIntervalMax_ = 0.06f;

#endif
};


