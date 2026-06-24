// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "WaterReflectionPass.h"

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

    /// @brief 水面反射用の補助 RenderView 要求を構築する
    std::vector<CoreEngine::RenderViewRequest> BuildRenderViewRequests() override;

    /// @brief 現在の DXR 水面屈折用波面データを返す
    const CoreEngine::WaterRefractionSurfaceData* GetWaterRefractionSurfaceData() const override;

    /// @brief 解放
    void Finalize() override;

private:
    void SetupWaterReflectionView(CoreEngine::ICamera* mainCamera, float planeHeight);
    void RestoreWaterReflectionView(CoreEngine::ICamera* mainCamera);
    void ApplyWaterRenderViewResult(const CoreEngine::RenderViewResult& result);
    const WaterConstants* GetCurrentWaterConstants() const;
    float GetCurrentWaterHeight() const;

#ifdef USE_IMGUI
    /// @brief 水面パラメータ ImGui パネルを描画する
    void DrawWaterImGui();

    /// @brief 指定したプリセットを WaterPlaneObject と ImGui キャッシュに適用する
    void ApplyWaterPreset(WaterPresetType preset);

    /// @brief プリセット設定に基づいてレイヤー分けされた Gerstner Wave を再生成する
    void RegenerateLayeredWaves(WaterPresetType preset, uint32_t activeWaveCount);

    /// @brief 指定プリセットの推奨波本数をUIと水面へ反映する
    void RestoreRecommendedWaveCount(WaterPresetType preset);
#endif

    void UpdateWaterRefractionSurfaceData();

    /// @brief 水面グリッドメッシュオブジェクト
    WaterPlaneObject* waterPlane_ = nullptr;

    /// @brief 地面モデルオブジェクト
    ModelObject* groundObject_ = nullptr;

    /// @brief 水面平面反射パス（Step 4）
    WaterReflectionPass reflectionPass_;

    CoreEngine::WaterRefractionSurfaceData waterRefractionSurfaceData_{};

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
    float imguiRTRefractionOffsetPixels_ = 3.0f;    ///< DXR 屈折の最大スクリーンずれ量（px）

    // ---- Depth Fade ----
    bool  imguiDepthFadeEnabled_ = true;            ///< Depth Fade 有効フラグ
    float imguiAbsorptionCoeff_  = 1.2f;            ///< 光吸収係数
    float imguiShallowColor_[3]  = { 0.10f, 0.85f, 0.65f };  ///< 浅瀬の水色
    float imguiDeepColor_[3]     = { 0.02f, 0.08f, 0.45f }; ///< 深場の水色
    bool  imguiDepthFadeDebugEnabled_ = false;      ///< Depth Fade デバッグ表示
    float imguiDepthFadeDebugScale_ = 1.5f;         ///< Depth Fade デバッグ表示倍率
    int   imguiDepthDebugViewMode_ = static_cast<int>(WaterDebugViewMode::RawDepth); ///< 水面デバッグ可視化モード

#endif
};


