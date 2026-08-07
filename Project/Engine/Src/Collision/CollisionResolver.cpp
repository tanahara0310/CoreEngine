#include "pch.h"
#include "CollisionResolver.h"
#include "Collider.h"
#include "GameObject/GameObject.h"

namespace CoreEngine
{
namespace CollisionResolver
{
    namespace {
        /// @brief 押し出しの対象になりうるか（Static でなく、オーナーが居る）
        bool IsPushable(const Collider& collider)
        {
            return !collider.IsStatic() && collider.GetOwner() != nullptr;
        }

        /// @brief オーナーを delta だけ動かす
        /// @return 動かせたら true
        bool Push(Collider& collider, const Vector3& delta)
        {
            GameObject* owner = collider.GetOwner();
            return owner ? owner->TryApplyCollisionPush(delta) : false;
        }
    }

    bool Resolve(Collider& a, Collider& b, const Geometry::Contact& contact)
    {
        // 通知専用のコライダーは押し出さない
        if (a.IsTrigger() || b.IsTrigger()) {
            return false;
        }
        if (contact.depth <= 0.0f) {
            return false;
        }

        // normal は a から b へ向かう。a を -normal、b を +normal へ動かすと離れる。
        const Vector3 separation = contact.normal * contact.depth;

        const bool pushableA = IsPushable(a);
        const bool pushableB = IsPushable(b);

        if (!pushableA && !pushableB) {
            return false;   // 両方とも動かせない（壁同士など）
        }

        if (pushableA && pushableB) {
            // 双方動かせるなら半分ずつ負担する
            const Vector3 half = separation * 0.5f;
            const bool movedA = Push(a, half * -1.0f);
            const bool movedB = Push(b, half);

            if (movedA && movedB) {
                return true;
            }
            // 片方が押し出しを受け付けなかった場合は、動いた側へ残りを寄せる
            if (movedA) { return Push(a, half * -1.0f); }
            if (movedB) { return Push(b, half); }
            return false;
        }

        if (pushableA) {
            if (Push(a, separation * -1.0f)) { return true; }
            return Push(b, separation);   // 受け付けなかったので相手を動かす
        }

        if (Push(b, separation)) { return true; }
        return Push(a, separation * -1.0f);
    }
}
}
