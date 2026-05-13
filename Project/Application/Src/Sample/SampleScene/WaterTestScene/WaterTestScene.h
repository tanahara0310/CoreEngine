// シーン
#include "Scene/BaseScene.h"

//エンジンシステム
#include "EngineSystem/EngineSystem.h"

#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"

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
    /// @brief 水面グリッドメッシュオブジェクト
    WaterPlaneObject* waterPlane_ = nullptr;
};

