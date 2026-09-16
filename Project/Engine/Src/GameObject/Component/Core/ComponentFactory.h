#pragma once

#include "IComponent.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Macro/UniqueName.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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

    /// @brief 実行時に分かる型の生成関数
    using RuntimeCreator = std::function<std::unique_ptr<IComponent>()>;

    /// @brief 試作した 1 個から読む型名と記述子
    struct Probe {
        std::string typeName;
        const Reflection::TypeDescriptor* descriptor = nullptr;
#ifdef USE_IMGUI
        std::string inspectorName;
#endif
    };
    using ProbeFunction = Probe (*)();

    static ComponentFactory& Get();

    /// @brief 生成関数を予約する
    /// @note 静的初期化中に呼ばれるのでインスタンスは作らない。型名の解決は `Prime()` が行う。
    void Reserve(Creator creator, ProbeFunction probe);

    /// @brief 予約を型名へ解決する（エンジン起動時に 1 回だけ）
    /// @note 型名と記述子を読むために 1 個ずつ試作して即座に捨てる。二度目以降の呼び出しは何もしない。
    void Prime();

    /// @brief 実行時に分かる型を登録する
    /// @param typeName `IComponent::GetTypeName()` が返す綴り
    /// @param creator 生成関数
    /// @param descriptor 型の記述子（無ければ nullptr）
    /// @param inspectorName インスペクタでの表示名
    /// @param sourceFile 型を書いたファイル（分からなければ空）
    /// @return 同じ型名が登録済みなら、登録せずに false
    /// @note スクリプトのクラスのように、起動してから分かる型が使う。
    ///       `Prime()` の前に登録した型名が C++ の型と重なっていたら、`Prime()` が C++ の型に置き換える。
    bool RegisterRuntime(const std::string& typeName, RuntimeCreator creator,
        const Reflection::TypeDescriptor* descriptor, const std::string& inspectorName,
        std::filesystem::path sourceFile = {});

    /// @brief `RegisterRuntime()` で登録した型をすべて外す
    void UnregisterRuntimeTypes();

    /// @brief 型名からコンポーネントを生成する
    /// @return 未登録の型なら nullptr
    std::unique_ptr<IComponent> Create(const std::string& typeName) const;

    /// @brief 型名からその型の記述子を引く
    /// @return 未登録の型か、記述子を持たない型なら nullptr（`Prime()` の前も nullptr）
    const Reflection::TypeDescriptor* FindDescriptor(const std::string& typeName) const;

    /// @brief その型名で生成できるか
    bool IsRegistered(const std::string& typeName) const;

    /// @brief `RegisterRuntime()` で登録した型か（スクリプトのクラスなど）
    bool IsRuntimeType(const std::string& typeName) const;

#ifdef USE_IMGUI
    /// @brief 型名からインスペクタでの表示名を引く
    /// @return 未登録の型なら空
    std::string GetInspectorName(const std::string& typeName) const;

    /// @brief 型を書いたファイル（`RegisterRuntime()` で渡したもの。無ければ空）
    std::filesystem::path GetSourceFile(const std::string& typeName) const;

    /// @brief 新しく作ったときのプロパティの値（保存形）
    /// @return 記述子を持たない型か、作れなかったら nullptr
    /// @note 初めて引いたときに 1 個作って控える。実行時の型を外すと控えも消える。
    const json* GetDefaultParameters(const std::string& typeName);
#endif

    /// @brief 登録済みの型名一覧（綴り順）
    std::vector<std::string> GetRegisteredTypeNames() const;

    /// @brief 登録済みの型数
    size_t GetRegisteredCount() const { return entries_.size(); }

    /// @brief 型名の解決（`Prime()`）が済んだか
    bool IsPrimed() const { return primed_; }

private:
    ComponentFactory() = default;
    ~ComponentFactory() = default;
    ComponentFactory(const ComponentFactory&) = delete;
    ComponentFactory& operator=(const ComponentFactory&) = delete;

    struct Reservation {
        Creator creator = nullptr;
        ProbeFunction probe = nullptr;
    };

    /// @brief 型名を解決した 1 型
    struct Entry {
        RuntimeCreator creator;
        const Reflection::TypeDescriptor* descriptor = nullptr;

        /// `RegisterRuntime()` で登録した型か
        bool runtime = false;
#ifdef USE_IMGUI
        std::string inspectorName;
        std::filesystem::path sourceFile;

        /// 新しく作ったときのプロパティの値（まだ作っていなければ空）
        std::optional<json> defaults;
#endif
    };

    /// 型名の解決前に溜めておく予約
    std::vector<Reservation> reservations_;

    /// 型名 → 生成関数と記述子
    std::unordered_map<std::string, Entry> entries_;

    bool primed_ = false;
};

/// @brief `COMPONENT_REGISTER` が使う自己登録ヘルパ
template <typename T>
struct AutoRegisterComponent {
    AutoRegisterComponent()
    {
        ComponentFactory::Get().Reserve(
            []() -> std::unique_ptr<IComponent> { return std::make_unique<T>(); },
            []() -> ComponentFactory::Probe {
                T probe;
                ComponentFactory::Probe result{ probe.GetTypeName(), probe.GetTypeDescriptor() };
#ifdef USE_IMGUI
                result.inspectorName = probe.GetInspectorName();
#endif
                return result;
            });
    }
};
}

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
            CORE_UNIQUE_NAME(kAutoRegisterComponent_){};                               \
    }
