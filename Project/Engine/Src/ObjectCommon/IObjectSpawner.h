#pragma once
#include <memory>

namespace CoreEngine {
    class GameObject;

    /// @brief ゲームオブジェクトのスポーンインターフェース
    /// @note GameObjectManager が実装し、GameObject::Spawn<T>() から使用される
    class IObjectSpawner {
    public:
        virtual ~IObjectSpawner() = default;

        /// @brief ゲームオブジェクトをスポーン（型消去後の共通経路）
        /// @param obj スポーンするオブジェクトの unique_ptr
        /// @return 登録されたオブジェクトへの生ポインタ
        virtual GameObject* SpawnRaw(std::unique_ptr<GameObject> obj) = 0;
    };
}
