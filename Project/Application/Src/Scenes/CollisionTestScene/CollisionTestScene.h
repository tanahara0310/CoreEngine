#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

#include "ProbeComponent.h"
#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

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

        /// @brief プローブ 1 個をコンポーネント合成で組む（mesh が nullptr なら見た目なし）
        ProbeComponent* MakeProbe(const std::string& label, const CoreEngine::Vector3& position,
            std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> mesh,
            const CoreEngine::Vector4& baseColor);

        /// @brief 見た目を持たないプローブ（描画コンポーネント無しでも判定が効くことの確認用）
        ProbeComponent* MakeHeadlessProbe(const std::string& label, const CoreEngine::Vector3& position);

        /// @brief 球プローブを生成（半径・レイヤー・色をまとめて設定）
        ProbeComponent* MakeSphereProbe(const std::string& label, const CoreEngine::Vector3& position,
            float radius, CoreEngine::CollisionLayer layer, const CoreEngine::Vector4& baseColor);

        /// @brief 箱プローブを生成
        ProbeComponent* MakeBoxProbe(const std::string& label, const CoreEngine::Vector3& position,
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

        ProbeComponent* t1Static_ = nullptr;  ///< T1: 球×球
        ProbeComponent* t1Mover_  = nullptr;

        ProbeComponent* t2Mover_ = nullptr;   ///< T2: 球→箱（球を先に登録）
        ProbeComponent*    t2Box_   = nullptr;

        ProbeComponent*    t3Box_   = nullptr;   ///< T3: 箱→球（箱を先に登録）
        ProbeComponent* t3Mover_ = nullptr;

        ProbeComponent* t4Item_  = nullptr;   ///< T4: レイヤーフィルタ
        ProbeComponent* t4Mover_ = nullptr;

        ProbeComponent* t5A_ = nullptr;       ///< T5: Default×Default
        ProbeComponent* t5B_ = nullptr;

        ProbeComponent* t6Survivor_ = nullptr;  ///< T6: 破棄時の Exit
        ProbeComponent* t6Victim_   = nullptr;

        ProbeComponent* t7Static_  = nullptr;  ///< T7: 生成/破棄の繰り返し（アドレス再利用）
        ProbeComponent* t7Spawned_ = nullptr;
        int t7SpawnCount_ = 0;

        ProbeComponent* t8A_ = nullptr;       ///< T8: スケール反映
        ProbeComponent* t8B_ = nullptr;

        ProbeComponent* t9Far_  = nullptr;  ///< T9: GetWorldPosition 未オーバーライド
        ProbeComponent* t9Near_ = nullptr;

        ProbeComponent* t10Static_  = nullptr;  ///< T10: コールバック中の RemoveCollider
        ProbeComponent* t10Remover_ = nullptr;

        ProbeComponent* t11Body_    = nullptr;  ///< T11: 1 オブジェクトに 2 本のコライダー
        ProbeComponent* t11BodyHit_ = nullptr;
        ProbeComponent* t11AtkHit_  = nullptr;

        ProbeComponent*    t12Wall_   = nullptr;   ///< T12: 壁にぶつかって止まる（押し出し）
        ProbeComponent* t12Pusher_ = nullptr;

        ProbeComponent* t14Near_ = nullptr;     ///< T14: レイキャスト問い合わせ
        ProbeComponent* t14Far_  = nullptr;
    };
}
