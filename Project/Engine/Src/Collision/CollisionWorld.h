#pragma once

#include "Collider.h"
#include "CollisionConfig.h"
#include "BroadPhase/IBroadPhase.h"
#include "Math/Geometry/RayCast.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief 衝突判定の実行とシーンへの問い合わせ
/// @details 登録されたコライダー間の判定（Step）と、レイキャスト・オーバーラップの
///          問い合わせ（Raycast / Overlap*）を提供する。

namespace CoreEngine
{
/// @brief レイキャストの結果
struct RaycastHit {
    Collider*   collider = nullptr;      ///< 当たったコライダー
    GameObject* object = nullptr;        ///< その所有者
    float       distance = 0.0f;         ///< レイ原点からの距離
    Vector3     point{ 0.0f, 0.0f, 0.0f };   ///< 交点
    Vector3     normal{ 0.0f, 0.0f, 0.0f };  ///< 交点の法線
};

/// @brief 衝突ワールド
/// @note コライダーの所有権は GameObject 側。ここは毎フレーム登録し直される
///       生ポインタを借用するだけ。
class CollisionWorld {
public:
    /// @brief レイヤー間の衝突マトリクスを指定して構築する（config は寿命を保証すること）
    explicit CollisionWorld(CollisionConfig* config);
    ~CollisionWorld();

    // ===== 登録 =====

    /// @brief コライダーを登録する
    void RegisterCollider(Collider* collider);

    /// @brief 登録されているコライダーをすべてクリア（衝突履歴は保持）
    void ClearColliders();

    /// @brief 登録と衝突履歴をすべてクリア（シーン遷移時など）
    void Clear();

    /// @brief 登録されているすべてのコライダーを取得
    const std::vector<Collider*>& GetAllColliders() const { return colliders_; }

    // ===== 判定 =====

    /// @brief 1 フレーム分の衝突判定を実行する
    /// @details ブロードフェーズ（AABB とレイヤーで候補を絞る）→ ナローフェーズ
    ///          （実形状で判定）→ 押し出し → Enter/Stay/Exit の発火、の順。
    void Step();

    // ===== ブロードフェーズの選択 =====

    /// @brief 使用するブロードフェーズの種類
    enum class BroadPhaseType {
        BruteForce,   ///< 全ペア総当たり（件数が少ないうちはこれが最速）
        UniformGrid,  ///< 一様グリッド（件数が増えたとき）
    };

    /// @brief ブロードフェーズを切り替える
    void SetBroadPhase(BroadPhaseType type);
    BroadPhaseType GetBroadPhaseType() const { return broadPhaseType_; }

    /// @brief 一様グリッドのセルサイズを設定する（UniformGrid のときのみ意味がある）
    void SetGridCellSize(float cellSize);

    // ===== 問い合わせ =====
    /// @note いずれも **直近の Step() 時点の登録内容**を見る。GameObject の
    ///       OnUpdate（判定フェーズより前）から呼ぶと 1 フレーム前の状態になる。

    /// @brief レイを飛ばして最も近いヒットを返す
    /// @param ray       レイ（direction は正規化しておくこと）
    /// @param layerMask 対象レイヤーのビット集合（既定は全レイヤー）
    bool Raycast(const Geometry::Ray& ray, float maxDistance,
                 uint64_t layerMask = ~uint64_t{ 0 }, RaycastHit* outHit = nullptr) const;

    /// @brief レイに当たるすべてのコライダーを距離順に返す
    void RaycastAll(const Geometry::Ray& ray, float maxDistance,
                    uint64_t layerMask, std::vector<RaycastHit>& outHits) const;

    /// @brief 球と重なるコライダーを集める
    void OverlapSphere(const Geometry::Sphere& sphere, uint64_t layerMask,
                       std::vector<Collider*>& outColliders) const;

    /// @brief AABB と重なるコライダーを集める
    void OverlapBox(const Geometry::AABB& box, uint64_t layerMask,
                    std::vector<Collider*>& outColliders) const;

    /// @brief 直近の Step() で 1 件以上と接触していたか
    /// @note デバッグ表示の色分けに使う。判定をやり直さず履歴を引くだけ。
    bool IsColliding(const Collider& collider) const;

    /// @brief 全レイヤーを対象にするマスク
    static constexpr uint64_t kAllLayers = ~uint64_t{ 0 };

private:
    /// @brief 衝突履歴のキー
    /// @details **コライダーの一意 ID で持つ**。以前はポインタで持っていたため、
    ///          解放されたコライダーと同じアドレスに新しいコライダーが載ると
    ///          古いペアと一致してしまう余地があった（ABA）。ID は再利用されない。
    struct PairKey {
        uint64_t first = 0;
        uint64_t second = 0;

        bool operator==(const PairKey& rhs) const noexcept {
            return first == rhs.first && second == rhs.second;
        }
    };

    /// @brief PairKey を unordered_map のキーにするためのハッシュ
    struct PairKeyHash {
        size_t operator()(const PairKey& key) const noexcept;
    };

    /// @brief ペアを構成するコライダー
    /// @note Exit を発火するには実体が要るのでキー（ID）とは別に持つ。
    struct PairColliders {
        Collider* a = nullptr;
        Collider* b = nullptr;
    };

    using PairMap = std::unordered_map<PairKey, PairColliders, PairKeyHash>;

    /// @brief ブロードフェーズへ proxy を詰める
    void BuildBroadPhase();

    std::vector<Collider*> colliders_;
    CollisionConfig* config_ = nullptr;

    // 前フレーム / 今フレームの衝突ペア。毎フレーム作り直さず swap して使い回す。
    PairMap previousCollisions_;
    PairMap currentCollisions_;

    BroadPhaseType broadPhaseType_ = BroadPhaseType::BruteForce;
    std::unique_ptr<BroadPhase::IBroadPhase> broadPhase_;
    float gridCellSize_ = 8.0f;

    // 毎フレーム使い回すワークバッファ
    std::vector<BroadPhase::PairIndices> candidatePairs_;

    /// 直近の Step() で接触していたコライダー ID（デバッグ表示の色分け用）
    std::unordered_set<uint64_t> collidingIds_;
};
}
