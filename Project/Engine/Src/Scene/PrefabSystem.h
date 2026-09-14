#pragma once

#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief プレハブ（コンポーネントの構成と値を持つアセット）と、そこから作ったオブジェクトの差分を扱う
    /// @details 差分の形は `{"prefab", "overrides", "addedComponents", "removedComponents"}`。
    ///          上書きのキーは `型名.プロパティ名`、同じ型の 2 個目以降は `型名[1].プロパティ名`。
    namespace PrefabSystem
    {
        /// @brief コンポーネントの有効・無効を表す上書きのプロパティ名
        inline constexpr const char* kEnabledProperty = "$enabled";

        /// @brief プレハブの `components` 配列を読む
        /// @return 見つからない・プレハブでない・読めなければ nullptr
        /// @note 読んだ内容は、ファイルの更新時刻が変わるまで使い回す。
        const json* LoadComponents(const Reflection::AssetRefValue& prefab);

        /// @brief オブジェクトの JSON からプレハブの参照を読む
        /// @return `prefab` が無い・読めなければ何も指さない値
        Reflection::AssetRefValue ReadPrefabRef(const json& instance);

        /// @brief オブジェクトの保存 JSON を、プレハブとの差分の形にする
        /// @param full `GameObject::Serialize()` の結果
        /// @param prefabComponents プレハブの `components` 配列
        /// @return `components` を差分に置き換えたもの（`prefab` は含まない）
        json MakeInstanceJson(const json& full, const json& prefabComponents);

        /// @brief 差分の形の JSON にプレハブの値を重ね、`GameObject::Deserialize()` が読める形にする
        /// @param instance シーンに保存されたオブジェクトの JSON
        /// @param prefabComponents プレハブの `components` 配列（読めなかったときは nullptr）
        /// @param problems 読み飛ばした指定の説明を足す先（要らなければ nullptr）
        /// @note `components` を持つ JSON は、差分の指定を外してそのまま返す。
        json ExpandInstanceJson(const json& instance, const json* prefabComponents,
                                std::vector<std::string>* problems = nullptr);
    }
}
