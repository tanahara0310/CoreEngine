#pragma once

#include "Math/Vector/Vector3.h"

#include <string>

class asIScriptEngine;

namespace CoreEngine
{
    class GameObject;
    class IComponent;
    class TransformComponent;
}

namespace CoreEngine::Script
{
    class ScriptGameObject;

    /// @brief スクリプトへ渡す Transform のハンドル
    /// @details GameObject のハンドルの一部で、参照カウントはそのハンドルと共有する。
    ///          使うたびに持ち主の GameObject から TransformComponent を引き直す。
    class ScriptTransform
    {
    public:
        explicit ScriptTransform(ScriptGameObject& owner) : owner_(owner) {}

        ScriptTransform(const ScriptTransform&) = delete;
        ScriptTransform& operator=(const ScriptTransform&) = delete;

        void AddRef() const;
        void Release() const;

        /// @brief 持ち主の GameObject に TransformComponent があるか
        bool Exists() const;

        Vector3 GetPosition() const;
        void SetPosition(const Vector3& position);

        /// @brief 回転（オイラー角・ラジアン）
        Vector3 GetRotation() const;
        void SetRotation(const Vector3& rotation);

        Vector3 GetScale() const;
        void SetScale(const Vector3& scale);

        /// @brief ワールド座標での位置
        Vector3 GetWorldPosition() const;

        /// @brief 親を設定する（nullptr で親なし。自分自身は親にしない）
        void SetParent(ScriptTransform* parent);

        /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
        ScriptGameObject* GetGameObject() const;

    private:
        /// @brief TransformComponent を引き、引けなければ 1 回だけ警告する
        TransformComponent* ResolveOrWarn(const char* action) const;

        ScriptGameObject& owner_;
        mutable bool warned_ = false;
    };

    /// @brief スクリプトへ渡す GameObject のハンドル
    /// @details 実体のポインタは持たず、使うたびにコンポーネントから持ち主を引き直す。
    ///          そのコンポーネントが壊れる（外された・持ち主が破棄された・シーンが終わった）と、実体の無いハンドルになる。
    class ScriptGameObject
    {
    public:
        /// @brief コンポーネントの持ち主を指すハンドルを作る
        /// @return 参照を 1 つ持ったハンドル
        /// @note そのコンポーネントを壊す前に `DetachComponent()` を呼ぶこと。
        static ScriptGameObject* CreateForOwner(const IComponent& component);

        ScriptGameObject(const ScriptGameObject&) = delete;
        ScriptGameObject& operator=(const ScriptGameObject&) = delete;

        void AddRef() const;
        void Release() const;

        /// @brief 指す先の実体（引けなければ nullptr）
        GameObject* Resolve() const;

        /// @brief 持ち主を引くコンポーネントを手放す（以降は実体の無いハンドルになる）
        void DetachComponent() noexcept { component_ = nullptr; }

        std::string GetName() const;
        void SetName(const std::string& name);
        bool IsActive() const;
        void SetActive(bool active);

        /// @brief 実体があり、破棄の予約もされていないか
        bool IsAlive() const;

        /// @brief 実体に破棄を予約する（実体の解放はフレーム末）
        void Destroy();

        /// @brief 同じ実体を指しているか（どちらかに実体が無ければ false）
        bool Equals(const ScriptGameObject& other) const;

        /// @brief この GameObject の Transform のハンドル（参照を 1 つ足して返す）
        ScriptTransform* GetTransform();

    private:
        ScriptGameObject() = default;
        ~ScriptGameObject() = default;

        /// @brief 実体を引き、引けなければ 1 回だけ警告する
        GameObject* ResolveOrWarn(const char* action) const;

        const IComponent* component_ = nullptr;
        ScriptTransform transform_{ *this };
        mutable int refCount_ = 1;
        mutable bool warned_ = false;
    };

    /// @brief `GameObject` と `Transform` の型をスクリプトへ登録する
    /// @details GameObject は名前・有効・生存の確認・破棄の予約・比較・Transform、
    ///          Transform は位置・回転・拡大・ワールド位置・親の設定を出す。どちらもスクリプトからは作れない。
    /// @return すべて登録できたら true
    bool RegisterGameObjectBinding(asIScriptEngine* engine);
}
