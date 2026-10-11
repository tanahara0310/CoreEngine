#pragma once

#ifdef CORE_EDITOR

#include "Utility/Lifetime/ScopedRegistration.h"

#include <functional>
#include <string>

namespace CoreEngine
{
    class GameObject;
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
    /// @return 登録を握るハンドル。破棄すると外れる（後から同じ型名を登録し直していれば、そちらを残す）
    ScopedRegistration Register(const std::string& typeName, Entry entry);

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

    /// @brief インスペクタの中身を、既定の欄の代わりに描く関数（スクリプトの CustomEditor）
    /// @param drawDefault 既定の欄を描く（値を変えたら true）
    /// @return 値を変えたら true
    using CustomDraw = std::function<bool(GameObject& object, IComponent& component, const std::function<bool()>& drawDefault)>;

    /// @brief 型名に、インスペクタの中身を描く関数を登録する（同じ型名は上書きする）
    ScopedRegistration RegisterCustomDraw(const std::string& typeName, CustomDraw draw);

    /// @brief インスペクタの中身を描く関数（登録が無ければ nullptr）
    const CustomDraw* FindCustomDraw(const IComponent& component);
}

#endif // CORE_EDITOR
