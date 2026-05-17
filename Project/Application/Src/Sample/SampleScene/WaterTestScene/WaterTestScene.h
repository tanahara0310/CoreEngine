// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
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

    /// @brief 解放
    void Finalize() override;

private:
#ifdef USE_IMGUI
    /// @brief 水面パラメータ ImGui パネルを描画する
    void DrawWaterImGui();
#endif

    /// @brief 水面グリッドメッシュオブジェクト
    WaterPlaneObject* waterPlane_ = nullptr;

    /// @brief 水面平面反射パス（Step 4）
    WaterReflectionPass reflectionPass_;

#ifdef USE_IMGUI
    // ---- ImGui 用キャッシュ（Material 側は Set/Get 経由で同期する） ----
    float imguiColor_[4] = { 0.04f, 0.18f, 0.28f, 1.0f }; ///< 水面ベースカラー RGBA
    float imguiRoughness_ = 0.04f;
    float imguiMetallic_ = 0.0f;
    bool  imguiIBLEnabled_ = true;
    float imguiScrollSpeed_[2] = { 0.03f, 0.01f }; ///< UV スクロール速度
    float imguiUVTiling_[2] = { 4.0f, 4.0f };      ///< UV タイリング
    bool  imguiReflectionEnabled_ = false;          ///< 反射テクスチャ有効フラグ（表示用）

    /// @brief テクスチャ使用モード
    /// 0 = テクスチャなし（ベースカラーのみ）
    /// 1 = ノーマルマップのみ
    /// 2 = アルベド + ノーマルマップ
    int imguiTextureMode_ = 1;
#endif
};


