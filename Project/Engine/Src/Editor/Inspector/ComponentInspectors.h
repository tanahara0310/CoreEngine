#pragma once

#ifdef CORE_EDITOR

#include <functional>
#include <string>

namespace CoreEngine
{
    class IComponent;
}

/// @brief コンポーネントの型ごとの、インスペクタでの出し方
namespace CoreEngine::Editor::ComponentInspectors
{
    /// @brief 型ごとの出し方
    struct Entry
    {
        std::string displayName;                   ///< 表示名（空なら記述子の表示名、それも無ければ型名）
        bool hidden = false;                       ///< インスペクタに出さない
        bool shownFirst = false;                   ///< ほかのコンポーネントより先に並べる
        std::function<bool(IComponent&)> drawBody; ///< 記述子で描けない欄（値を変えたら true）
        std::function<void(IComponent&)> drawExtra; ///< 欄の後に足す表示
    };

    /// @brief エンジンの型の出し方を登録する
    void RegisterEngineTypes();

    /// @brief 型の出し方を登録する（同じ型名は上書きする）
    void Register(const std::string& typeName, Entry entry);

    /// @brief コンポーネントの出し方（登録が無ければ nullptr）
    const Entry* Find(const IComponent& component);

    /// @brief インスペクタに出す名前
    std::string DisplayNameOf(const IComponent& component);

    /// @brief 型名からインスペクタに出す名前を引く（コンポーネントを作らない）
    std::string DisplayNameOf(const std::string& typeName);

    /// @brief インスペクタに出すか
    bool IsShown(const IComponent& component);

    /// @brief ほかのコンポーネントより先に並べるか
    bool IsShownFirst(const IComponent& component);
}

#endif // CORE_EDITOR
