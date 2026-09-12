#pragma once

#include "IComponent.h"

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
/// @brief 型名から `IComponent` を生成するレジストリ。
/// @details シーン JSON のように型が文字列でしか分からない経路が使う。
class ComponentFactory {
public:
    using Creator = std::unique_ptr<IComponent> (*)();
    using TypeNameProbe = std::string (*)();

    static ComponentFactory& Get();

    /// @brief 生成関数を予約する
    /// @note 静的初期化中に呼ばれるのでインスタンスは作らない。型名の解決は `Prime()` が行う。
    void Reserve(Creator creator, TypeNameProbe probe);

    /// @brief 予約を型名へ解決する（エンジン起動時に 1 回だけ）
    /// @note 型名を読むために 1 個ずつ試作して即座に捨てる。二度目以降の呼び出しは何もしない。
    void Prime();

    /// @brief 型名からコンポーネントを生成する
    /// @return 未登録の型なら nullptr
    std::unique_ptr<IComponent> Create(const std::string& typeName) const;

    /// @brief その型名で生成できるか
    bool IsRegistered(const std::string& typeName) const;

    /// @brief 登録済みの型名一覧（綴り順）
    std::vector<std::string> GetRegisteredTypeNames() const;

    /// @brief 登録済みの型数
    size_t GetRegisteredCount() const { return creators_.size(); }

private:
    ComponentFactory() = default;
    ~ComponentFactory() = default;
    ComponentFactory(const ComponentFactory&) = delete;
    ComponentFactory& operator=(const ComponentFactory&) = delete;

    struct Reservation {
        Creator creator = nullptr;
        TypeNameProbe probe = nullptr;
    };

    /// 型名の解決前に溜めておく予約
    std::vector<Reservation> reservations_;

    /// 型名 → 生成関数
    std::unordered_map<std::string, Creator> creators_;

    bool primed_ = false;
};

/// @brief `COMPONENT_REGISTER` が使う自己登録ヘルパ
template <typename T>
struct AutoRegisterComponent {
    AutoRegisterComponent()
    {
        ComponentFactory::Get().Reserve(
            []() -> std::unique_ptr<IComponent> { return std::make_unique<T>(); },
            []() -> std::string { T probe; return probe.GetTypeName(); });
    }
};
}

#define COMPONENT_REGISTER_DETAIL_CAT2(a, b) a##b
#define COMPONENT_REGISTER_DETAIL_CAT(a, b) COMPONENT_REGISTER_DETAIL_CAT2(a, b)

/// 対応する .cpp のファイルスコープで COMPONENT_REGISTER(型名) を書くと
/// `GetTypeName()` が返す綴りでその型を生成できるようになる。
///
/// 既定コンストラクタを持つ型にだけ書ける。引数が要る型は、依存を
/// ObjectRef へ移してから登録する。
#define COMPONENT_REGISTER(TypeName)                                                   \
    static_assert(std::is_default_constructible_v<TypeName>,                           \
        #TypeName " は既定コンストラクタを持たないので COMPONENT_REGISTER できない");   \
    namespace {                                                                        \
        const ::CoreEngine::AutoRegisterComponent<TypeName>                            \
            COMPONENT_REGISTER_DETAIL_CAT(kAutoRegisterComponent_, __LINE__){};        \
    }
