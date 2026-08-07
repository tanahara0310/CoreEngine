#include "pch.h"
#include "CollisionWorld.h"
#include "CollisionResolver.h"
#include "BroadPhase/BruteForceBroadPhase.h"
#include "BroadPhase/UniformGridBroadPhase.h"
#include "GameObject/GameObject.h"

#include <algorithm>

namespace CoreEngine
{
    namespace {
        /// @brief 64bit の混ぜ込み（splitmix64 の finalizer）
        inline uint64_t Mix64(uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        /// @brief ペアキーを正規化（小さい ID を first に）
        /// @details (a,b) と (b,a) を同一視するため。
        template <class T>
        std::pair<T, T> Ordered(T a, T b)
        {
            return (a < b) ? std::pair<T, T>{ a, b } : std::pair<T, T>{ b, a };
        }
    }

    size_t CollisionWorld::PairKeyHash::operator()(const PairKey& key) const noexcept
    {
        // 単純な XOR は近い値同士で上位ビットが消えてバケットが偏るので、必ず混ぜてから合成する。
        return static_cast<size_t>(Mix64(key.first) ^ (Mix64(key.second) * 0x9E3779B97F4A7C15ull));
    }

    CollisionWorld::CollisionWorld(CollisionConfig* config)
        : config_(config)
    {
        SetBroadPhase(BroadPhaseType::BruteForce);
    }

    CollisionWorld::~CollisionWorld() = default;

    //================================================
    // 登録
    //================================================

    void CollisionWorld::RegisterCollider(Collider* collider)
    {
        if (collider) {
            colliders_.push_back(collider);
        }
    }

    void CollisionWorld::ClearColliders()
    {
        // capacity は残す（毎フレーム同じくらいの件数を入れ直すため）
        colliders_.clear();
    }

    void CollisionWorld::Clear()
    {
        colliders_.clear();
        previousCollisions_.clear();
        currentCollisions_.clear();
        candidatePairs_.clear();
        if (broadPhase_) { broadPhase_->Clear(); }
    }

    //================================================
    // ブロードフェーズの選択
    //================================================

    void CollisionWorld::SetBroadPhase(BroadPhaseType type)
    {
        broadPhaseType_ = type;
        switch (type) {
        case BroadPhaseType::UniformGrid:
            broadPhase_ = std::make_unique<BroadPhase::UniformGridBroadPhase>(gridCellSize_);
            break;
        case BroadPhaseType::BruteForce:
        default:
            broadPhase_ = std::make_unique<BroadPhase::BruteForceBroadPhase>();
            break;
        }
    }

    void CollisionWorld::SetGridCellSize(float cellSize)
    {
        gridCellSize_ = cellSize;
        if (auto* grid = dynamic_cast<BroadPhase::UniformGridBroadPhase*>(broadPhase_.get())) {
            grid->SetCellSize(cellSize);
        }
    }

    //================================================
    // 判定
    //================================================

    void CollisionWorld::BuildBroadPhase()
    {
        broadPhase_->Clear();

        for (Collider* collider : colliders_) {
            BroadPhase::Proxy proxy;
            proxy.bounds     = collider->GetWorldAABB();
            proxy.layerIndex = static_cast<uint32_t>(collider->GetLayer());
            proxy.layerMask  = config_ ? config_->GetLayerMask(collider->GetLayer()) : ~uint64_t{ 0 };
            proxy.ownerId    = reinterpret_cast<uint64_t>(collider->GetOwner());
            proxy.isStatic   = collider->IsStatic();
            broadPhase_->Insert(proxy);
        }
    }

    void CollisionWorld::Step()
    {
        currentCollisions_.clear();

        // ── ブロードフェーズ: AABB とレイヤーで候補を絞る ──────────────
        BuildBroadPhase();
        candidatePairs_.clear();
        broadPhase_->CollectPairs(candidatePairs_);

        // ── ナローフェーズ: 実形状で判定する ───────────────────────────
        for (const auto& candidate : candidatePairs_) {
            Collider* a = colliders_[candidate.first];
            Collider* b = colliders_[candidate.second];

            // 接触情報（法線・貫通深度・接触点）は押し出しとコールバックの両方で使う。
            // normal は a から b へ向かう向き。b 側へ渡すときは反転する。
            Geometry::Contact contact{};
            if (!a->Intersects(*b, &contact)) {
                continue;
            }

            const auto ids = Ordered(a->GetId(), b->GetId());
            const PairKey key{ ids.first, ids.second };
            currentCollisions_.emplace(key, PairColliders{ a, b });

            // めり込み解消はコールバックより先。コールバックには解決前の
            // 接触情報（どれだけめり込んでいたか）を渡す。
            CollisionResolver::Resolve(*a, *b, contact);

            Geometry::Contact reversed = contact;
            reversed.normal = contact.normal * -1.0f;

            if (previousCollisions_.find(key) == previousCollisions_.end()) {
                a->OnCollisionEnter(b, contact);
                b->OnCollisionEnter(a, reversed);
            } else {
                a->OnCollisionStay(b, contact);
                b->OnCollisionStay(a, reversed);
            }
        }

        // ── 接触が切れたペアへ Exit を配る ─────────────────────────────
        // 「離れた」場合と「登録から外れた（破棄・非アクティブ化・無効化・取り外し）」場合を
        // 同じループで扱う。外れたコライダーは候補にも上がらないので、自動的にここへ落ちる。
        //
        // 参照の安全性: 登録から外れる 4 経路すべてで実体はこのフレーム中は生きている。
        //  - GameObject::Destroy()  … 破棄はフレーム末の CleanupDestroyed で遅延実行
        //  - SetActive(false)       … オブジェクトは生存
        //  - Collider::SetEnabled(false) … 同上
        //  - GameObject::RemoveCollider() … 実体はフレーム末まで retired_ が保持
        // 外れたペアは「外れた最初のフレーム」で履歴から消えるので、実体が解放される
        // 次フレーム以降に previousCollisions_ が触ることはない。
        for (const auto& entry : previousCollisions_) {
            if (currentCollisions_.find(entry.first) != currentCollisions_.end()) { continue; }

            entry.second.a->OnCollisionExit(entry.second.b);
            entry.second.b->OnCollisionExit(entry.second.a);
        }

        // 接触中の ID 集合を作り直す（デバッグ表示の色分け用）
        collidingIds_.clear();
        for (const auto& entry : currentCollisions_) {
            collidingIds_.insert(entry.first.first);
            collidingIds_.insert(entry.first.second);
        }

        // 集合は作り直さず入れ替えて使い回す（毎フレームの再確保を避ける）
        previousCollisions_.swap(currentCollisions_);
    }

    bool CollisionWorld::IsColliding(const Collider& collider) const
    {
        return collidingIds_.find(collider.GetId()) != collidingIds_.end();
    }

    //================================================
    // 問い合わせ
    //================================================

    namespace {
        /// @brief レイヤーマスクの対象か
        bool MatchesLayer(const Collider& collider, uint64_t layerMask)
        {
            return ((layerMask >> static_cast<uint32_t>(collider.GetLayer())) & 1u) != 0;
        }

        /// @brief コライダー 1 つに対するレイ判定
        bool RaycastCollider(const Collider& collider, const Geometry::Ray& ray,
                             float maxDistance, Geometry::RayHit& outHit)
        {
            if (collider.GetShapeType() == ColliderShapeType::Sphere) {
                return Geometry::Raycast(ray, collider.GetWorldSphere(), &outHit, 0.0f, maxDistance);
            }
            return Geometry::Raycast(ray, collider.GetWorldAABB(), &outHit, 0.0f, maxDistance);
        }
    }

    bool CollisionWorld::Raycast(const Geometry::Ray& ray, float maxDistance,
                                 uint64_t layerMask, RaycastHit* outHit) const
    {
        bool found = false;
        float nearest = maxDistance;
        RaycastHit best{};

        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }

            Geometry::RayHit hit{};
            if (!RaycastCollider(*collider, ray, nearest, hit)) { continue; }

            found = true;
            nearest = hit.distance;   // 以降はこれより手前だけを探す
            best.collider = collider;
            best.object   = collider->GetOwner();
            best.distance = hit.distance;
            best.point    = hit.point;
            best.normal   = hit.normal;
        }

        if (found && outHit) { *outHit = best; }
        return found;
    }

    void CollisionWorld::RaycastAll(const Geometry::Ray& ray, float maxDistance,
                                    uint64_t layerMask, std::vector<RaycastHit>& outHits) const
    {
        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }

            Geometry::RayHit hit{};
            if (!RaycastCollider(*collider, ray, maxDistance, hit)) { continue; }

            RaycastHit result;
            result.collider = collider;
            result.object   = collider->GetOwner();
            result.distance = hit.distance;
            result.point    = hit.point;
            result.normal   = hit.normal;
            outHits.push_back(result);
        }

        std::sort(outHits.begin(), outHits.end(),
            [](const RaycastHit& lhs, const RaycastHit& rhs) { return lhs.distance < rhs.distance; });
    }

    void CollisionWorld::OverlapSphere(const Geometry::Sphere& sphere, uint64_t layerMask,
                                       std::vector<Collider*>& outColliders) const
    {
        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }

            const bool hit = (collider->GetShapeType() == ColliderShapeType::Sphere)
                ? Geometry::Intersect(sphere, collider->GetWorldSphere())
                : Geometry::Intersect(sphere, collider->GetWorldAABB());
            if (hit) { outColliders.push_back(collider); }
        }
    }

    void CollisionWorld::OverlapBox(const Geometry::AABB& box, uint64_t layerMask,
                                    std::vector<Collider*>& outColliders) const
    {
        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }

            const bool hit = (collider->GetShapeType() == ColliderShapeType::Sphere)
                ? Geometry::Intersect(box, collider->GetWorldSphere())
                : Geometry::Intersect(box, collider->GetWorldAABB());
            if (hit) { outColliders.push_back(collider); }
        }
    }
}
