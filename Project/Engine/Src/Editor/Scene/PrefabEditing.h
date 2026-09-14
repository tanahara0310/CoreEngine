#pragma once

#ifdef USE_IMGUI

#include <string>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;
    class IComponent;
    struct AssetInfo;

    namespace Reflection
    {
        struct PropertyDescriptor;
    }

    /// @brief エディタからプレハブを作る・書き戻す・つながりを外す操作（どれも Undo に積む）
    namespace PrefabEditing
    {
        /// @brief オブジェクトの構成と値でプレハブを作り、そのオブジェクトをプレハブにつなぐ
        /// @param directory プレハブを置くフォルダ（プロジェクトの根からの相対パス）
        /// @return 作ったプレハブ（作れなければ nullptr）
        const AssetInfo* CreateFromObject(GameObject& object,
                                          const std::string& directory = "Application/Assets/Prefabs");

        /// @brief オブジェクトの今の構成と値をプレハブへ書き戻す
        /// @note 最初の Transform の位置と回転は、プレハブの値のまま残す。
        bool ApplyObject(GameObjectManager& manager, GameObject& object);

        /// @brief 1 つのプロパティの今の値をプレハブへ書き戻す
        bool ApplyProperty(GameObjectManager& manager, GameObject& object, IComponent& component,
                           const Reflection::PropertyDescriptor& property);

        /// @brief プレハブとのつながりを外す（以後はシーンへ構成ごと保存される）
        void Unlink(GameObject& object);
    }
}

#endif // USE_IMGUI
