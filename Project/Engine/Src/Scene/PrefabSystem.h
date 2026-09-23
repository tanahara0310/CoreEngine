#pragma once

#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;
    class IComponent;
    struct AssetInfo;

    /// @brief プレハブ（コンポーネントの構成と値を持つアセット）と、そこから作ったオブジェクトの差分を扱う
    /// @details 差分の形は `{"prefab", "overrides", "versions", "addedComponents", "removedComponents"}`。
    ///          上書きのキーは `型名.プロパティ名`、同じ型の 2 個目以降は `型名[1].プロパティ名`。
    ///          `versions` は、上書きを書いた型のうち版が 2 以上のものの `型名 → 版`。
    namespace PrefabSystem
    {
        /// @brief コンポーネントの有効・無効を表す上書きのプロパティ名
        inline constexpr const char* kEnabledProperty = "$enabled";

        /// @brief プレハブの `components` 配列を読む
        /// @return 見つからない・プレハブでない・読めなければ nullptr
        /// @note 各コンポーネントを型の今の版の形へ書き換えてから、ファイルの更新時刻が変わるまで使い回す。
        ///       `ComponentFactory::Prime()` の前に読んだ分は、その後に初めて読むときに書き換える。
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
        ///       上書きは、`versions` の版（書いていない型は 1）から型の今の版の形へ書き換えてから重ねる。
        json ExpandInstanceJson(const json& instance, const json* prefabComponents,
                                std::vector<std::string>* problems = nullptr);

        /// @brief 値が同じか（数値は float の精度で比べる）
        bool SameValue(const json& a, const json& b);

        /// @brief プレハブの `components` 配列から、オブジェクトのこのコンポーネントと同じ型・同じ順番の要素を探す
        /// @return 見つからなければ空
        std::optional<std::size_t> FindComponentIndex(const json& prefabComponents,
                                                      const GameObject& object, const IComponent& component);

        /// @brief オブジェクトの構成と値を、プレハブの `components` 配列の形にする
        /// @note シーン内の別オブジェクトへの参照（`{"ref": …}`）は null にする。
        json MakePrefabComponents(const GameObject& object);

        /// @brief プレハブからオブジェクトを 1 体作ってシーンへ登録する
        /// @return プレハブを読めなければ nullptr
        GameObject* Instantiate(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                                const std::string& name);

        /// @brief `components` 配列を新しいプレハブファイルに書き出し、AssetDatabase へ登録する
        /// @param path プロジェクトの根からの相対パス（`Application/Assets/Prefabs/Rock.prefab` など）
        /// @return 登録したアセット（書き出せない・登録できなければ nullptr）
        const AssetInfo* CreatePrefab(const std::string& path, const json& components);

        /// @brief プレハブの `components` 配列を書き換え、シーン内でそのプレハブから作ったオブジェクトへ反映する
        /// @details 各オブジェクトは、書き換える前のプレハブとの差分を保ったまま、残りの値を新しいプレハブにそろえる。
        /// @return 書き出せなければ false
        bool UpdatePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                          const json& components);
    }
}
