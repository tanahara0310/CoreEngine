#pragma once

#include "Scene/BaseScene.h"
#include "../../TestGameObject/SphereObject.h"

class CollisionTestScene : public CoreEngine::BaseScene {
public:
    /// @brief 初期化
    /// @param engine エンジンシステムへのポインタ 
    void Initialize(CoreEngine::EngineSystem* engine) override;

    /// @brief 描画
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新処理（GameObjectの更新前）
    void OnUpdate() override;

private:
    static constexpr float kSphereRadius = 1.0f;
    static constexpr int   kStaticSphereCount = 4;

    // プレイヤー球体のデフォルトカラー（青系）
    const CoreEngine::Vector4 kPlayerColor = { 0.3f, 0.5f, 1.0f, 1.0f };
    // 静止球体のデフォルトカラー
    const CoreEngine::Vector4 kStaticColors[kStaticSphereCount] = {
        { 1.0f, 0.4f, 0.4f, 1.0f },  // 赤系
        { 0.4f, 1.0f, 0.4f, 1.0f },  // 緑系
        { 1.0f, 1.0f, 0.4f, 1.0f },  // 黄系
        { 1.0f, 0.4f, 1.0f, 1.0f },  // 紫系
    };

    // キーボードで操作する球体
    SphereObject* playerSphere_ = nullptr;

    // 静置された球体群
    SphereObject* staticSpheres_[kStaticSphereCount] = {};
};


