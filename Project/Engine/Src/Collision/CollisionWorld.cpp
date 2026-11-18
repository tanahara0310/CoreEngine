#include "pch.h"
#include "CollisionWorld.h"
#include "CollisionResolver.h"
#include "BroadPhase/BruteForceBroadPhase.h"
#include "BroadPhase/UniformGridBroadPhase.h"
#include "GameObject/GameObject.h"
#include "Utility/Random/Hash.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace CoreEngine
{
    namespace {
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
        return static_cast<size_t>(Hash::Mix64(key.first) ^ (Hash::Mix64(key.second) * 0x9E3779B97F4A7C15ull));
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
        CollectContacts(contacts_);
        DispatchEvents(contacts_);
    }

    void CollisionWorld::CollectContacts(std::vector<ContactPair>& outPairs)
    {
        outPairs.clear();

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

            outPairs.push_back(ContactPair{ a, b, contact });
        }
    }

    void CollisionWorld::DispatchEvents(const std::vector<ContactPair>& pairs)
    {
        currentCollisions_.clear();

        for (const auto& pair : pairs) {
            Collider* a = pair.a;
            Collider* b = pair.b;

            const auto ids = Ordered(a->GetId(), b->GetId());
            const PairKey key{ ids.first, ids.second };
            currentCollisions_.emplace(key, PairColliders{ a, b });

            // めり込み解消はコールバックより先。コールバックには解決前の
            // 接触情報（どれだけめり込んでいたか）を渡す。
            CollisionResolver::Resolve(*a, *b, pair.contact);

            Geometry::Contact reversed = pair.contact;
            reversed.normal = pair.contact.normal * -1.0f;

            if (previousCollisions_.find(key) == previousCollisions_.end()) {
                a->OnCollisionEnter(b, pair.contact);
                b->OnCollisionEnter(a, reversed);
            } else {
                a->OnCollisionStay(b, pair.contact);
                b->OnCollisionStay(a, reversed);
            }
        }

        // ── 接触が切れたペアへ Exit を配る ─────────────────────────────
        // 「離れた」場合と「登録から外れた」場合を同じループで扱う（外れたものは候補に上がらない）。
        // 参照の安全性: 外れる 4 経路（Destroy / SetActive(false) / SetEnabled(false) /
        // RemoveCollider）すべてで実体はフレーム末まで生きており、履歴はその前に消える。
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

    uint64_t CollisionWorld::GetLayerMask(CollisionLayer layer) const
    {
        return config_ ? config_->GetLayerMask(layer) : kAllLayers;
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

        /// @brief 前絞りで箱を広げる量（固定の分と、座標の大きさに比例する分）
        constexpr float kPrefilterMargin = 0.01f;
        constexpr float kPrefilterRelative = 1e-5f;

        /// @brief 前絞りに使う、少し広げた箱
        Geometry::AABB Inflate(const Geometry::AABB& box)
        {
            const float magnitude = (std::max)({
                std::abs(box.min.x), std::abs(box.min.y), std::abs(box.min.z),
                std::abs(box.max.x), std::abs(box.max.y), std::abs(box.max.z) });
            const float margin = kPrefilterMargin + magnitude * kPrefilterRelative;
            const Vector3 grow{ margin, margin, margin };
            return Geometry::AABB{ box.min - grow, box.max + grow };
        }

        /// @brief 2 つの箱が離れているのが確かか（NaN が混ざると離れていない扱い）
        bool Separated(const Geometry::AABB& a, const Geometry::AABB& b)
        {
            return a.min.x > b.max.x || a.max.x < b.min.x
                || a.min.y > b.max.y || a.max.y < b.min.y
                || a.min.z > b.max.z || a.max.z < b.min.z;
        }

        /// @brief 中心と半分の大きさから、符号によらず包む箱を作る
        Geometry::AABB QueryBounds(const Vector3& center, const Vector3& half)
        {
            const Vector3 extent{ std::abs(half.x), std::abs(half.y), std::abs(half.z) };
            return Inflate(Geometry::AABB{ center - extent, center + extent });
        }

        /// @brief レイが 0 から maxDistance までの間に箱を通りうるか
        /// @note 起点が箱の中なら通る扱い。NaN が混ざると通る扱い。
        bool RayMayHit(const Geometry::Ray& ray, const Geometry::AABB& box, float maxDistance)
        {
            const float origin[3]    = { ray.origin.x, ray.origin.y, ray.origin.z };
            const float direction[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
            const float low[3]       = { box.min.x, box.min.y, box.min.z };
            const float high[3]      = { box.max.x, box.max.y, box.max.z };

            float enter = 0.0f;
            float exit = maxDistance;
            for (int axis = 0; axis < 3; ++axis) {
                if (direction[axis] == 0.0f) {
                    if (origin[axis] < low[axis] || origin[axis] > high[axis]) { return false; }
                    continue;
                }
                const float inverse = 1.0f / direction[axis];
                float t0 = (low[axis] - origin[axis]) * inverse;
                float t1 = (high[axis] - origin[axis]) * inverse;
                if (t0 > t1) { std::swap(t0, t1); }
                if (t0 > enter) { enter = t0; }
                if (t1 < exit) { exit = t1; }
                if (enter > exit) { return false; }
            }
            return true;
        }

        /// @brief コライダー 1 つに対するレイ判定
        bool RaycastCollider(const Collider& collider, const Geometry::Ray& ray,
                             float maxDistance, Geometry::RayHit& outHit)
        {
            return VisitWorldShape(collider, [&](const auto& shape) {
                return Geometry::Raycast(ray, shape, &outHit, 0.0f, maxDistance);
            });
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

            if (!RayMayHit(ray, Inflate(collider->GetWorldAABB()), nearest)) { continue; }

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

            if (!RayMayHit(ray, Inflate(collider->GetWorldAABB()), maxDistance)) { continue; }

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
        const float reach = std::abs(sphere.radius);
        const Geometry::AABB queryBounds = QueryBounds(sphere.center, Vector3{ reach, reach, reach });

        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }
            if (Separated(queryBounds, collider->GetWorldAABB())) { continue; }

            const bool hit = VisitWorldShape(*collider, [&](const auto& shape) {
                return Geometry::Intersect(sphere, shape);
            });
            if (hit) { outColliders.push_back(collider); }
        }
    }

    void CollisionWorld::OverlapBox(const Geometry::AABB& box, uint64_t layerMask,
                                    std::vector<Collider*>& outColliders) const
    {
        // 問い合わせの箱も向きを持つ形として扱い、判定を OBB へ一本化する
        const Geometry::OBB queryBox{ box.GetCenter(), box.GetSize() * 0.5f };
        const Geometry::AABB queryBounds = QueryBounds(queryBox.center, queryBox.halfExtents);

        for (Collider* collider : colliders_) {
            if (!collider->IsEnabled() || !MatchesLayer(*collider, layerMask)) { continue; }
            if (Separated(queryBounds, collider->GetWorldAABB())) { continue; }

            const bool hit = VisitWorldShape(*collider, [&](const auto& shape) {
                return Geometry::Intersect(queryBox, shape);
            });
            if (hit) { outColliders.push_back(collider); }
        }
    }
}
