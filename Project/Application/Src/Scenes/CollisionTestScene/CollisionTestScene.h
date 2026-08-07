#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

#include "CollisionProbeObject.h"

namespace CollisionTest
{
    /// @brief 当たり判定システムの回帰テストシーン（リファクタリングの安全網）
    /// @details
    ///  当たり判定はこれまでアプリ側から一切使われておらず、壊れても気づけない状態だった。
    ///  このシーンは Enter/Stay/Exit・レイヤーフィルタ・破棄時の挙動・スケール反映を
    ///  決まったフレームスケジュールで自動実行し、結果を App Editor の
    ///  "Collision Test" パネルに PASS / FAIL で表示する。
    ///
    ///  ＜重要な設計＞
    ///  - すべてのプローブ位置は毎フレーム OnUpdate() で再指定する。
    ///    シーン JSON の復元やギズモ操作でテストがずれないようにするため。
    ///  - 結果の評価は OnLateUpdate()（PostObjectUpdate の後）で行う。
    ///    OnUpdate() では今フレームの判定結果がまだ出ていない。
    ///  - FAIL のうち knownBug 列に番号があるものは既知バグの再現であり、
    ///    Phase 1 の修正後に PASS へ変わることが完了条件。
    class CollisionTestScene : public CoreEngine::BaseScene {
    public:
        void OnInitialize() override;

    protected:
        void OnUpdate() override;
        void OnLateUpdate() override;

    private:
        // ===== フレームスケジュール =====
        static constexpr int kTraverseStart  = 30;   ///< 通過テスト開始フレーム
        static constexpr int kTraverseFrames = 120;  ///< 通過にかけるフレーム数
        static constexpr int kTraverseEnd    = kTraverseStart + kTraverseFrames;
        static constexpr int kStaticJudge    = 40;   ///< 常時重なり系を判定するフレーム
        static constexpr int kDestroyFrame   = 60;   ///< T6 で victim を破棄するフレーム
        static constexpr int kDestroyJudge   = 70;   ///< T6 を判定するフレーム
        static constexpr int kAbaStart       = 80;   ///< T7 のスポーン/破棄サイクル開始
        static constexpr int kAbaCycleFrames = 16;   ///< 1 サイクルのフレーム数
        static constexpr int kAbaCycles      = 6;    ///< サイクル数
        static constexpr int kAbaJudge       = kAbaStart + kAbaCycleFrames * kAbaCycles + 8;

        static constexpr float kTraverseMinX = -6.0f;
        static constexpr float kTraverseMaxX = 6.0f;

        // T12: 壁へ向かって毎フレーム前進する速度と、止まるべき位置
        static constexpr float kPushSpeed     = 0.1f;   ///< 1 フレームあたりの前進量
        static constexpr float kPushStartX    = -6.0f;
        static constexpr float kPushExpectedX = -2.0f;  ///< 壁面(-1) - 半径(1)
        static constexpr int   kPushJudge     = 120;    ///< 到達に十分なフレーム数
        static constexpr float kProbeY       = 1.5f;
        static constexpr float kRowSpacingZ  = 5.0f;

        // ===== 構築ヘルパー =====

        /// @brief 球プローブを生成（半径・レイヤー・色をまとめて設定）
        SphereProbe* MakeSphereProbe(const std::string& label, const CoreEngine::Vector3& position,
            float radius, CoreEngine::CollisionLayer layer, const CoreEngine::Vector4& baseColor);

        /// @brief 箱プローブを生成
        BoxProbe* MakeBoxProbe(const std::string& label, const CoreEngine::Vector3& position,
            float size, CoreEngine::CollisionLayer layer, const CoreEngine::Vector4& baseColor);

        /// @brief 通過テストの現在 X 座標を求める
        float TraverseX() const;

        /// @brief 各行の Z 座標
        static float RowZ(int row) { return static_cast<float>(row) * kRowSpacingZ; }

        // ===== 各テストの評価 =====
        void EvaluateTraverseCases();
        void EvaluateStaticCases();
        void EvaluateDestroyCase();
        void UpdateAbaCase();
        void PublishOptInCase();
        void EvaluateMultiColliderCase();
        void EvaluatePushOutCase();
        void EvaluateContactInfoCase();
        void EvaluateRaycastQueryCase();

        // ===== 状態 =====
        int frame_ = 0;

        SphereProbe* t1Static_ = nullptr;  ///< T1: 球×球
        SphereProbe* t1Mover_  = nullptr;

        SphereProbe* t2Mover_ = nullptr;   ///< T2: 球→箱（球を先に登録）
        BoxProbe*    t2Box_   = nullptr;

        BoxProbe*    t3Box_   = nullptr;   ///< T3: 箱→球（箱を先に登録）
        SphereProbe* t3Mover_ = nullptr;

        SphereProbe* t4Item_  = nullptr;   ///< T4: レイヤーフィルタ
        SphereProbe* t4Mover_ = nullptr;

        SphereProbe* t5A_ = nullptr;       ///< T5: Default×Default
        SphereProbe* t5B_ = nullptr;

        SphereProbe* t6Survivor_ = nullptr;  ///< T6: 破棄時の Exit
        SphereProbe* t6Victim_   = nullptr;

        SphereProbe* t7Static_  = nullptr;  ///< T7: 生成/破棄の繰り返し（アドレス再利用）
        SphereProbe* t7Spawned_ = nullptr;
        int t7SpawnCount_ = 0;

        SphereProbe* t8A_ = nullptr;       ///< T8: スケール反映
        SphereProbe* t8B_ = nullptr;

        HeadlessProbe* t9Far_  = nullptr;  ///< T9: GetWorldPosition 未オーバーライド
        HeadlessProbe* t9Near_ = nullptr;

        SphereProbe* t10Static_  = nullptr;  ///< T10: コールバック中の RemoveCollider
        SphereProbe* t10Remover_ = nullptr;

        SphereProbe* t11Body_    = nullptr;  ///< T11: 1 オブジェクトに 2 本のコライダー
        SphereProbe* t11BodyHit_ = nullptr;
        SphereProbe* t11AtkHit_  = nullptr;

        BoxProbe*    t12Wall_   = nullptr;   ///< T12: 壁にぶつかって止まる（押し出し）
        SphereProbe* t12Pusher_ = nullptr;

        SphereProbe* t14Near_ = nullptr;     ///< T14: レイキャスト問い合わせ
        SphereProbe* t14Far_  = nullptr;
    };
}
