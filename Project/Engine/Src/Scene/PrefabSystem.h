#pragma once

#include "GameObject/GameObjectTags.h"
#include "GameObject/ObjectId.h"
#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;
    class IComponent;
    struct AssetInfo;

    /// @brief プレハブ（根と子の、タグとコンポーネントの構成と値を持つアセット）と、そこから作ったオブジェクトの差分を扱う
    /// @details ファイルの形は `{"tag", "components", "children"}`（`tag` は Untagged のとき、`children` は子が無いとき書かない）。
    ///          子の形は `{"node", "name", "active", "tag", "components"}`。子の中の参照は `ref` にプレハブの中の番号を書く。
    ///          差分の形は `{"prefab", "tag", "overrides", "versions", "addedComponents", "removedComponents"}`。
    ///          子の差分は `prefab` の代わりに `prefabRoot`（根の ID）と `prefabNode`（プレハブの中の番号）を持つ。
    ///          `tag` はプレハブと違うときだけ書く。
    ///          上書きのキーは `型名.プロパティ名`、同じ型の 2 個目以降は `型名[1].プロパティ名`。
    ///          `versions` は、上書きを書いた型のうち版が 2 以上のものの `型名 → 版`。
    namespace PrefabSystem
    {
        /// @brief コンポーネントの有効・無効を表す上書きのプロパティ名
        inline constexpr const char* kEnabledProperty = "$enabled";

        /// @brief プレハブの中で根を指す番号
        inline constexpr const char* kRootNode = "0000000000000001";

        /// @brief 子の差分が持つ、根の ID のキー
        inline constexpr const char* kPrefabRootKey = "prefabRoot";

        /// @brief 子の差分が持つ、プレハブの中の番号のキー
        inline constexpr const char* kPrefabNodeKey = "prefabNode";

        /// @brief プレハブの子 1 体分
        struct PrefabChild
        {
            std::string node;                             ///< プレハブの中の番号（16 桁の 16 進）
            std::string name;                             ///< 作ったオブジェクトの名前
            bool active = true;                           ///< 作ったオブジェクトの有効・無効
            std::string tag = GameObjectTags::kUntagged;  ///< 作ったオブジェクトに付けるタグ
            json components = json::array();              ///< コンポーネントの構成と値

            bool operator==(const PrefabChild&) const = default;
        };

        /// @brief プレハブの中身
        struct PrefabContents
        {
            std::string tag = GameObjectTags::kUntagged;  ///< 作ったオブジェクトに付けるタグ
            json components = json::array();              ///< コンポーネントの構成と値
            std::vector<PrefabChild> children;            ///< 子と孫（親が子より先に並ぶ）
        };

        /// @brief 置いた物 1 組の、プレハブの中の番号 → シーンの ID（どちらも 16 桁の 16 進）
        using NodeIdMap = std::unordered_map<std::string, std::string>;

        /// @brief シーンの置いた物全部の、根の ID → その組の番号と ID の対応
        using InstanceIndex = std::unordered_map<ObjectId, NodeIdMap>;

        /// @brief プレハブを読む
        /// @return 見つからない・プレハブでない・読めなければ nullptr
        /// @note 各コンポーネントを型の今の版の形へ書き換えてから、ファイルの更新時刻が変わるまで使い回す。
        ///       `ComponentFactory::Prime()` の前に読んだ分は、その後に初めて読むときに書き換える。
        const PrefabContents* Load(const Reflection::AssetRefValue& prefab);

        /// @brief プレハブの子を番号で探す
        /// @return その番号の子が無ければ nullptr
        const PrefabChild* FindChild(const PrefabContents& prefab, std::string_view node);

        /// @brief プレハブの中に、プレハブの中の番号で指す参照があるか（子を持つプレハブには必ずある）
        /// @return プレハブを読めなければ false
        bool HasReferences(const Reflection::AssetRefValue& prefab);

        /// @brief オブジェクトの JSON からプレハブの参照を読む
        /// @return `prefab` が無い・読めなければ何も指さない値
        Reflection::AssetRefValue ReadPrefabRef(const json& instance);

        /// @brief オブジェクトの保存 JSON を、プレハブとの差分の形にする
        /// @param full `GameObject::Serialize()` の結果
        /// @param prefab 元のプレハブの中身
        /// @return `components` を差分に置き換えたもの（`prefab` は含まない）
        json MakeInstanceJson(const json& full, const PrefabContents& prefab);

        /// @brief 差分の形の JSON にプレハブの値を重ね、`GameObject::Deserialize()` が読める形にする
        /// @param instance シーンに保存されたオブジェクトの JSON
        /// @param prefab 元のプレハブの中身（読めなかったときは nullptr）
        /// @param problems 読み飛ばした指定の説明を足す先（要らなければ nullptr）
        /// @note `components` を持つ JSON は、差分の指定を外してそのまま返す。
        ///       上書きは、`versions` の版（書いていない型は 1）から型の今の版の形へ書き換えてから重ねる。
        json ExpandInstanceJson(const json& instance, const PrefabContents* prefab,
                                std::vector<std::string>* problems = nullptr);

        /// @brief 値が同じか（数値は float の精度で比べる）
        bool SameValue(const json& a, const json& b);

        /// @brief 中身が同じか（コンポーネントの数値は float の精度で比べる）
        bool SameValue(const PrefabContents& a, const PrefabContents& b);

        /// @brief プレハブの `components` 配列から、オブジェクトのこのコンポーネントと同じ型・同じ順番の要素を探す
        /// @return 見つからなければ空
        std::optional<std::size_t> FindComponentIndex(const json& prefabComponents,
                                                      const GameObject& object, const IComponent& component);

        /// @brief 根と、番号を持つ子孫を、プレハブの中身の形にする
        /// @param nodes 子孫の ID → プレハブの中の番号（ここに無い子孫は入れない）
        /// @note 組の中を指す参照はプレハブの中の番号へ付け替え、組の外を指す参照は null にする。
        PrefabContents MakePrefabContents(const GameObjectManager& manager, const GameObject& root,
                                          const std::unordered_map<ObjectId, std::string>& nodes);

        /// @brief プレハブの中の番号を新しく作る（`used` と重ならないものを返し、`used` へ足す）
        std::string MakeNode(std::vector<std::string>& used);

        /// @brief 置いた物の根と、その子の印を持つオブジェクトから、番号 → ID の対応を作る（根の番号も入る）
        NodeIdMap CollectInstanceNodes(const GameObjectManager& manager, const GameObject& root);

        /// @brief シーンの置いた物全部の、根の ID → 番号と ID の対応を作る
        InstanceIndex BuildInstanceIndex(const GameObjectManager& manager);

        /// @brief オブジェクトが属する置いた物の根
        /// @return 根ならそのオブジェクト、プレハブの子ならその根。どちらでもない・根が無ければ nullptr
        GameObject* FindInstanceRoot(const GameObjectManager& manager, const GameObject& object);

        /// @brief プレハブの根か子 1 体分を、置いた物の ID へ付け替えて取り出す
        /// @param node プレハブの中の番号（根は `kRootNode`）
        /// @param ids その置いた物の番号 → ID
        /// @return タグと構成だけを持つ中身（子は持たない）。その番号がプレハブに無ければ値なし
        std::optional<PrefabContents> ResolveNode(const PrefabContents& prefab, std::string_view node,
                                                  const NodeIdMap& ids);

        /// @brief 置いた物の 1 体が、差分を比べる相手
        /// @param index 作ってあれば渡す（無ければその組だけを調べる）
        /// @return オブジェクトが根でも子でもない・プレハブを読めない・その番号がプレハブに無ければ値なし
        std::optional<PrefabContents> ResolveBase(const GameObjectManager& manager, const GameObject& object,
                                                  const InstanceIndex* index = nullptr);

        /// @brief プレハブから根と子を作ってシーンへ登録する
        /// @param serializeKey 根の保存キー（空なら名前から決める）。子の保存キーは `根の保存キー_子の名前`
        /// @param outChildren 作った子の書き出し先（要らなければ nullptr）
        /// @return 根（プレハブを読めなければ nullptr）
        GameObject* Instantiate(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                                const std::string& name, const std::string& serializeKey = {},
                                std::vector<GameObject*>* outChildren = nullptr);

        /// @brief 中身を新しいプレハブファイルに書き出し、AssetDatabase へ登録する
        /// @param path プロジェクトの根からの相対パス（`Application/Assets/Prefabs/Rock.prefab` など）
        /// @return 登録したアセット（書き出せない・登録できなければ nullptr）
        const AssetInfo* CreatePrefab(const std::string& path, const PrefabContents& contents);

        /// @brief プレハブの中身を書き換え、シーン内でそのプレハブから作ったオブジェクトへ反映する
        /// @details 根と子のそれぞれが、書き換える前のプレハブとの差分を保ったまま、残りの値を新しいプレハブにそろえる。
        /// @return 書き出せなければ false
        bool UpdatePrefab(GameObjectManager& manager, const Reflection::AssetRefValue& prefab,
                          const PrefabContents& contents);
    }
}
