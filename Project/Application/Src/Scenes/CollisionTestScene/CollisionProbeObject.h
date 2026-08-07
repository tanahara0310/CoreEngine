#pragma once

#include "GameObject/GameObject.h"
#include "GameObject/Model/ModelGameObject.h"
#include "Collision/CollisionInfo.h"
#include "Math/Vector/Vector4.h"

#include "GameObjects/Primitive/PrimitiveSphereObject.h"
#include "GameObjects/Primitive/CubeObject.h"

#include <string>
#include <type_traits>

namespace CollisionTest
{
    /// @brief 衝突イベントの発生回数と、最後に受け取った接触情報
    struct ProbeStats {
        int enter = 0;   ///< OnCollisionEnter の呼ばれた回数
        int stay  = 0;   ///< OnCollisionStay の呼ばれた回数
        int exit  = 0;   ///< OnCollisionExit の呼ばれた回数
        int overlapDepth = 0;  ///< enter - exit（同時接触数。0 に戻らなければ Exit 取りこぼし）

        /// 最後に Enter で受け取った接触情報（CollisionInfo の配線確認用）
        CoreEngine::Vector3 lastNormal{};
        float               lastDepth = 0.0f;
        bool                lastInfoValid = false;

        void Reset() {
            enter = stay = exit = overlapDepth = 0;
            lastNormal = {};
            lastDepth = 0.0f;
            lastInfoValid = false;
        }
    };

    /// @brief プローブ共通のイベント処理（テンプレート外に置いてコード重複を避ける）
    namespace ProbeEvents
    {
        /// @param label ログに出すプローブ名
        /// @param stats 更新するカウンタ
        /// @param other 衝突相手（nullptr 可）
        void OnEnter(const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);
        void OnStay (const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);
        void OnExit (const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);

        /// @brief A-2（コールバック中の RemoveCollider）再現がオプトインされているか
        bool IsRemoveColliderInCallbackEnabled();
    }

    /// @brief 衝突イベントを数える可視プローブ
    /// @tparam Base ModelGameObject 派生の見た目クラス（PrimitiveSphereObject / CubeObject）
    /// @details 判定結果を色で示す（接触中は hitColor、離れたら baseColor）。
    ///          リファクタリング前後でこの色の変化タイミングが変わらないことが
    ///          目視での回帰確認になる。
    template <class Base>
    class ModelProbe : public Base {
        static_assert(std::is_base_of_v<CoreEngine::ModelGameObject, Base>,
            "Base must derive from ModelGameObject");

    public:
        using Base::Base;

        // ===== 衝突イベント =====

        /// @brief 接触情報つき版。法線・貫通深度を記録してから既定の転送に流す。
        /// @note 基底の既定実装が OnCollisionEnter(GameObject*) を呼ぶので、
        ///       旧 API のオーバーライド（下）もそのまま動く＝両経路の同時検証になる。
        void OnCollisionEnter(const CoreEngine::CollisionInfo& info) override
        {
            stats_.lastNormal = info.normal;
            stats_.lastDepth = info.depth;
            stats_.lastInfoValid = true;
            Base::OnCollisionEnter(info);
        }

        void OnCollisionEnter(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnEnter(label_, stats_, other);
            ApplyColor();

            // A-2 の再現: コールバック中にコライダーを即時解放する。
            // 判定ループは colliders_ に生ポインタを保持したままなので use-after-free になる。
            if (removeColliderOnEnter_ && ProbeEvents::IsRemoveColliderInCallbackEnabled()) {
                this->RemoveCollider();
            }
        }

        void OnCollisionStay(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnStay(label_, stats_, other);
        }

        void OnCollisionExit(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnExit(label_, stats_, other);
            ApplyColor();
        }

        // ===== テスト用アクセサ =====

        ProbeStats&       Stats()       { return stats_; }
        const ProbeStats& Stats() const { return stats_; }

        /// @brief ログ・パネル表示に使う名前を設定（GameObject の表示名も揃える）
        void SetLabel(const std::string& label)
        {
            label_ = label;
            this->SetName(label);
        }
        const std::string& GetLabel() const { return label_; }

        /// @brief 非接触時の色を設定
        void SetBaseColor(const CoreEngine::Vector4& color)
        {
            baseColor_ = color;
            ApplyColor();
        }

        /// @brief 接触中の色を設定
        void SetHitColor(const CoreEngine::Vector4& color) { hitColor_ = color; }

        /// @brief OnCollisionEnter 内で RemoveCollider() を呼ぶ（A-2 再現用）
        void SetRemoveColliderOnEnter(bool enable) { removeColliderOnEnter_ = enable; }

    private:
        /// @brief 現在の接触状態に応じてマテリアル色を反映
        void ApplyColor()
        {
            auto* model = this->GetModel();
            if (!model) { return; }
            auto* material = model->GetMaterial();
            if (!material) { return; }

            material->SetColor(stats_.overlapDepth > 0 ? hitColor_ : baseColor_);
        }

        ProbeStats  stats_;
        std::string label_ = "Probe";

        CoreEngine::Vector4 baseColor_ = { 0.75f, 0.78f, 0.82f, 1.0f };
        CoreEngine::Vector4 hitColor_  = { 0.95f, 0.25f, 0.20f, 1.0f };

        bool removeColliderOnEnter_ = false;
    };

    /// 球プローブ（半径・分割数はコンストラクタ引数）
    using SphereProbe = ModelProbe<PrimitiveSphereObject>;

    /// 箱プローブ（サイズはコンストラクタ引数）
    using BoxProbe = ModelProbe<CubeObject>;

    /// @brief 見た目を持たないプローブ（ModelGameObject を経由しない派生の検証用）
    /// @details GameObject を直接継承する。`GetWorldPosition()` は純粋仮想なので
    ///          実装を強制される（A-3 の「オーバーライド忘れで全員が原点」は
    ///          コンパイルエラーになったため、実行時には再現できなくなった）。
    ///          このクラスは「正しく実装すれば ModelGameObject 以外でも判定が効く」
    ///          ことを確認する役割に変わっている。
    class HeadlessProbe : public CoreEngine::GameObject {
    public:
        const char* GetObjectName() const override { return "HeadlessProbe"; }

        CoreEngine::Vector3 GetWorldPosition() const override { return logicalPosition_; }

        void OnCollisionEnter(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnEnter(label_, stats_, other);
        }
        void OnCollisionStay(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnStay(label_, stats_, other);
        }
        void OnCollisionExit(CoreEngine::GameObject* other) override
        {
            ProbeEvents::OnExit(label_, stats_, other);
        }

        ProbeStats&       Stats()       { return stats_; }
        const ProbeStats& Stats() const { return stats_; }

        void SetLabel(const std::string& label) { label_ = label; this->SetName(label); }

        /// @brief 位置を設定（GetWorldPosition がそのまま返す = 判定に効く）
        void SetLogicalPosition(const CoreEngine::Vector3& p) { logicalPosition_ = p; }
        const CoreEngine::Vector3& GetLogicalPosition() const { return logicalPosition_; }

    private:
        ProbeStats          stats_;
        std::string         label_ = "HeadlessProbe";
        CoreEngine::Vector3 logicalPosition_{};
    };
}
