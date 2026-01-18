#pragma once
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <cassert>

/// @brief コンポーネント管理クラス
/// テンプレートを使用して型安全なコンポーネント登録・取得を提供

namespace CoreEngine
{
class ComponentManager {
public:
    /// @brief コンポーネントを登録
    /// @tparam T コンポーネントの型
    /// @param component コンポーネントへの生ポインタ（所有権は移譲しない）
    template<typename T>
    void Register(T* component) {
        std::type_index typeIdx(typeid(T));
        components_[typeIdx] = component;
    }

    /// @brief コンポーネントを取得
    /// @tparam T コンポーネントの型
    /// @return コンポーネントへのポインタ（登録されていない場合nullptr）
    template<typename T>
    T* Get() {
        std::type_index typeIdx(typeid(T));
        auto it = components_.find(typeIdx);
        if (it != components_.end()) {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }

    /// @brief コンポーネントが登録されているか確認
    /// @tparam T コンポーネントの型
    /// @return 登録されている場合true
    template<typename T>
    bool Has() const {
        std::type_index typeIdx(typeid(T));
        return components_.find(typeIdx) != components_.end();
    }

    /// @brief 全てのコンポーネント登録を解除
    void Clear() {
        components_.clear();
    }

private:
    /// @brief 型情報をキーとしたコンポーネントマップ
    /// void* として保存し、取得時に適切な型にキャスト
    std::unordered_map<std::type_index, void*> components_;
};
}
