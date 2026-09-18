#pragma once

#include "Collision/CollisionLayer.h"
#include "Math/Vector/Vector3.h"

class asIScriptEngine;

namespace CoreEngine
{
    struct CollisionInfo;
}

namespace CoreEngine::Script
{
    class ScriptGameObject;

    /// @brief スクリプトへ渡す接触の情報
    /// @details 相手の GameObject のハンドルと、通知したときの値の写しを持つ。スクリプトが控えても値は変わらない。
    class ScriptCollision
    {
    public:
        /// @param trigger どちらかのコライダーがトリガーか
        /// @return 参照を 1 つ持ったオブジェクト
        static ScriptCollision* Create(const CollisionInfo& info, bool trigger);

        ScriptCollision(const ScriptCollision&) = delete;
        ScriptCollision& operator=(const ScriptCollision&) = delete;

        void AddRef() const;
        void Release() const;

        /// @brief 相手の GameObject のハンドル（参照を 1 つ足して返す。相手が無ければ nullptr）
        ScriptGameObject* GetGameObject() const;

        /// @brief 自分から相手へ向かう押し出しの向き（Exit ではゼロ）
        Vector3 GetNormal() const { return normal_; }

        /// @brief めり込みの深さ（Exit では 0）
        float GetDepth() const { return depth_; }

        /// @brief 接触点（ワールド座標）
        Vector3 GetPoint() const { return point_; }

        /// @brief 相手のコライダーのレイヤー
        CollisionLayer GetLayer() const { return layer_; }

        /// @brief 自分のコライダーのレイヤー（1 つのオブジェクトが複数のコライダーを持つときの区別に使う）
        CollisionLayer GetSelfLayer() const { return selfLayer_; }

        /// @brief どちらかのコライダーがトリガーか
        bool IsTrigger() const { return trigger_; }

    private:
        ScriptCollision() = default;
        ~ScriptCollision();

        ScriptGameObject* other_ = nullptr;
        Vector3 normal_{};
        float depth_ = 0.0f;
        Vector3 point_{};
        CollisionLayer layer_ = CollisionLayer::Default;
        CollisionLayer selfLayer_ = CollisionLayer::Default;
        bool trigger_ = false;
        mutable int refCount_ = 1;
    };

    /// @brief 当たり判定の型をスクリプトへ登録する
    /// @details 列挙 `CollisionLayer` と、接触の関数に渡す `Collision` を出す。
    /// @return すべて登録できたら true
    bool RegisterPhysicsBinding(asIScriptEngine* engine);
}
