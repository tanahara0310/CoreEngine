#pragma once

#include "IComponent.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace CoreEngine
{
class GameObject;

/// @brief コンポーネントを保持する器（`GameObject` が継承する）。
/// @details 実体は個別ヒープ確保なので追加しても既存の `T*` は無効化されず（参照安定）、
///          取り外しはスロットを nullptr 化してフレーム末に解放する（遅延解放）。
class ComponentHost {
public:
    ComponentHost() = default;
    /// @brief デストラクタ（保持しているコンポーネントを追加の逆順で破棄する）
    virtual ~ComponentHost();

    ComponentHost(const ComponentHost&) = delete;
    ComponentHost& operator=(const ComponentHost&) = delete;

    // ===== 追加 =====

    /// @brief コンポーネントを生成してアタッチする（以後の追加でも戻り値は無効化されない）
    /// @note `Awake()` はこの中で即座に呼ばれる。兄弟コンポーネントを見る初期化は `Start()` に書くこと。
    ///       `DataAttachScope` の外で付けたものは、コードが付けたもの（`IComponent::IsAttachedByCode()`）になる。
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<IComponent, T>,
            "T must derive from IComponent");

        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        raw->owner_ = ownerObject_;
        raw->attachedByCode_ = dataAttachDepth_ == 0;
        components_.push_back(std::move(owned));
        raw->Awake();
        return raw;
    }

    /// @brief 生成済みのコンポーネントをアタッチする
    /// @param component 所有権を渡すコンポーネント（nullptr なら何もしない）
    /// @param invokeAwake ここで `Awake()` を呼ぶか
    /// @return アタッチされたコンポーネント。渡されたものが nullptr なら nullptr
    /// @note 型が実行時の文字列でしか分からない経路（シーン JSON からの復元）が使う。
    ///       `MeshRendererComponent` のようにモデルのロードを `Awake()` で行う型があるので、
    ///       復元側は false を渡して値を流し終えてから自分で `Awake()` を呼ぶ。
    IComponent* AttachComponent(std::unique_ptr<IComponent> component, bool invokeAwake = true);

    // ===== 取得 =====

    /// @brief 指定型のコンポーネントを 1 個取得する（見つからなければ nullptr）
    /// @tparam T 具象型でも基底型でもよい。`IComponent` を継承していないミックスイン
    ///           （`ITransformSource` など）も `dynamic_cast` のクロスキャストで引ける。
    template <typename T>
    T* GetComponent() const {
        for (const auto& component : components_) {
            if (!component) { continue; }
            if (auto* typed = dynamic_cast<T*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    /// @brief 指定型のコンポーネントをすべて取得する（同型を複数付けている場合）
    /// @tparam T `GetComponent` と同じく具象型・基底型・ミックスインのいずれでもよい
    template <typename T>
    std::vector<T*> GetComponents() const {
        std::vector<T*> result;
        for (const auto& component : components_) {
            if (!component) { continue; }
            if (auto* typed = dynamic_cast<T*>(component.get())) {
                result.push_back(typed);
            }
        }
        return result;
    }

    /// @brief 指定型のコンポーネントを持っているか
    template <typename T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    /// @brief 指定型のコンポーネントを取得し、無ければ生成する
    /// @note 移行期の互換 API（`GameObject::AddSphereCollider()` 等）が使う。
    template <typename T, typename... Args>
    T* GetOrAddComponent(Args&&... args) {
        if (auto* existing = GetComponent<T>()) {
            return existing;
        }
        return AddComponent<T>(std::forward<Args>(args)...);
    }

    /// @brief アタッチされているコンポーネント数（取り外し済みは含まない）
    size_t GetComponentCount() const;

    /// @brief 全コンポーネントを列挙する（**nullptr スロットが混ざりうる**ので要チェック）
    const std::vector<std::unique_ptr<IComponent>>& GetAllComponents() const {
        return components_;
    }

    // ===== 削除 =====

    /// @brief 指定したコンポーネントを取り外す（実体の解放はフレーム末）
    /// @return 見つかって取り外したら true
    bool RemoveComponent(IComponent* component);

    /// @brief 指定型のコンポーネントをすべて取り外す（実体の解放はフレーム末）
    /// @return 取り外した個数
    template <typename T>
    size_t RemoveComponents() {
        size_t removed = 0;
        for (auto& component : components_) {
            if (!component) { continue; }
            if (dynamic_cast<T*>(component.get())) {
                RetireSlot(component);
                ++removed;
            }
        }
        return removed;
    }

    /// @brief すべてのコンポーネントを取り外す（実体の解放はフレーム末）
    void RemoveAllComponents();

    /// @brief 取り外し済みコンポーネントの実体を解放し、配列を詰め直す
    /// @return 解放したものがあれば true
    /// @note `GameObjectManager` がフレーム末（衝突判定の後）に呼ぶ。
    bool ReleaseRetiredComponents();

    // ===== エディタからの着脱 =====

    /// @brief スコープの間に付いたコンポーネントを、コードが付けたものとして扱わない
    /// @note シーン JSON・プレハブの復元と、エディタでオブジェクトを作る・コンポーネントを足す操作が使う。
    class DataAttachScope {
    public:
        explicit DataAttachScope(ComponentHost& host) : host_(host) { ++host_.dataAttachDepth_; }
        ~DataAttachScope() { --host_.dataAttachDepth_; }

        DataAttachScope(const DataAttachScope&) = delete;
        DataAttachScope& operator=(const DataAttachScope&) = delete;

    private:
        ComponentHost& host_;
    };

    /// @brief コンポーネントを外し、実体はオブジェクトが破棄されるまで控える
    /// @return 外す前の位置（取り外し済みを除いた並びでの添え字）。付いていなければ空
    /// @note `OnDestroy()` は呼ばない。兄弟が控えたポインタは、破棄まで指したまま使える。
    ///       控えたものは `ReattachComponent()` で付け直せる。更新ループの外から呼ぶこと。
    std::optional<std::size_t> DetachComponent(IComponent* component);

    /// @brief `DetachComponent()` で外したコンポーネントを付け直す
    /// @param position 取り外し済みを除いた並びでの添え字（付いている数以上なら末尾）
    /// @return 外したものの中に無ければ false
    bool ReattachComponent(IComponent* component, std::size_t position);

    /// @brief `DetachComponent()` で外して控えているコンポーネントを探す
    /// @return 控えていなければ nullptr
    IComponent* FindDetachedComponent(const IComponent* component) const;

    /// @brief 保存形からコンポーネントを作り、指定の位置へ付ける
    /// @param entry `SerializeComponents` が書いた配列の 1 要素
    /// @param position 取り外し済みを除いた並びでの添え字（付いている数以上なら末尾）
    /// @return 付けたコンポーネント（保存形が壊れていれば nullptr）
    /// @note コードが付けたものとして扱わない。値を流し終えてから `Awake()` を呼ぶ。
    IComponent* RestoreComponent(const json& entry, std::size_t position);

    /// @brief コンポーネントの位置（取り外し済みを除いた並びでの添え字）
    /// @return 付いていなければ空
    std::optional<std::size_t> FindComponentPosition(const IComponent* component) const;

    // ===== ライフサイクル発行（GameObjectManager が呼ぶ） =====

    /// @brief まだ Start() を呼んでいないコンポーネントの Start() を呼ぶ
    void DispatchComponentStart();

    /// @brief 有効なコンポーネントの Update() を呼ぶ
    void DispatchComponentUpdate();

    /// @brief 有効なコンポーネントの LateUpdate() を呼ぶ
    void DispatchComponentLateUpdate();

    /// @brief 全コンポーネントの OnDestroy() を呼ぶ（オブジェクト破棄時）
    /// @note 実体はまだ解放しない。二重呼び出しはしない。
    void DispatchComponentDestroy();

    // ===== シリアライズ =====

    /// @brief アタッチされているコンポーネントを JSON 配列へ書き出す
    /// @return `[{ "type", "enabled", "version", "parameters" }, ...]`。中身が無ければ空配列
    /// @note 値は型記述子から取る。記述子が無い型は、保存データをそのまま持つ口（`IRawSavedParameters`）から取る。
    ///       `version` は記述子の版が 2 以上の型だけに書く。
    json SerializeComponents() const;

    /// @brief 1 つのコンポーネントを `SerializeComponents` の配列の 1 要素の形で書き出す
    static json SerializeComponent(const IComponent& component);

    /// @brief JSON 配列からコンポーネントの状態を復元する
    /// @param components `SerializeComponents` が書いた形
    /// @note 既にアタッチされているものへ型名で順に対応づけて値を流す。
    ///       同じ型を複数持つ場合は並び順で対応する。
    ///       対応するものが無ければ `ComponentFactory` で生成してアタッチし、
    ///       ファクトリにも無い型はエラーを出し、保存データを持ち続ける `MissingComponent` を付ける。
    ///       ここで付いたものは、コードが付けたものとして扱わない。
    void DeserializeComponents(const json& components);

protected:
    /// @brief オーナーの GameObject を登録する
    /// @note `GameObject` のコンストラクタが `this` を渡す。`ComponentHost` は
    ///       `GameObject` の定義を知らないので自分でキャストはできない。
    void SetOwnerObject(GameObject* owner) { ownerObject_ = owner; }

private:
    /// @brief スロットを retired_ へ移し、配列上は nullptr にする（インデックス不変）
    void RetireSlot(std::unique_ptr<IComponent>& slot);

    /// @brief 取り外し済みを除いた並びで position 番目の手前へ入れる
    void InsertAtPosition(std::unique_ptr<IComponent> component, std::size_t position);

    /// @brief 保存形の 1 要素の有効・版・値をコンポーネントへ流す
    static void LoadComponentEntry(IComponent& target, const json& entry);

    GameObject* ownerObject_ = nullptr;

    /// 生きているコンポーネント（実体は個別確保 = 参照が安定 / nullptr スロットがありうる）
    std::vector<std::unique_ptr<IComponent>> components_;

    /// 取り外し済みコンポーネントの墓場（フレーム末まで実体を保持する）
    std::vector<std::unique_ptr<IComponent>> retired_;

    /// `DetachComponent()` で外したコンポーネント（オブジェクトの破棄まで実体を保持する）
    std::vector<std::unique_ptr<IComponent>> detached_;

    /// `DataAttachScope` の入れ子の深さ（0 より大きい間に付いたものは、コードが付けたものではない）
    int dataAttachDepth_ = 0;

    /// OnDestroy() を発行済みか（二重発行の防止）
    bool destroyDispatched_ = false;
};
}
