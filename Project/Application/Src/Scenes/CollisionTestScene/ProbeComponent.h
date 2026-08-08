#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "WorldTransform/WorldTransform.h"

#include <string>

namespace CollisionTest
{
    /// @brief 1 プローブ分の衝突イベント集計
    struct ProbeStats {
        int enter = 0;
        int stay = 0;
        int exit = 0;
        int overlapDepth = 0;  ///< enter - exit（同時接触数。0 に戻らなければ Exit 取りこぼし）

        // 直近の接触情報（Phase 4 の押し出し検証用）
        CoreEngine::Vector3 lastNormal{};
        float lastDepth = 0.0f;
        bool  lastInfoValid = false;

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
        void OnEnter(const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);
        void OnStay (const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);
        void OnExit (const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other);

        /// @brief A-2（コールバック中の RemoveCollider）再現がオプトインされているか
        bool IsRemoveColliderInCallbackEnabled();
    }

    /// @brief 衝突イベントを数え、接触状態を色で示すコンポーネント。
    /// @details `ColliderComponent` のコールバックを購読するだけなので、**見た目の種類
    ///          （球・箱・見た目なし）を問わず同じ 1 行で載る**。以前は
    ///          `template<class Base> class ModelProbe : public Base` という CRTP で
    ///          見た目クラスごとに実体化していた。
    class ProbeComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "Probe"; }

        /// @brief コライダーのイベントを購読し、初期色を適用する
        void Start() override;

        // ===== 設定 =====

        /// @brief ログ・パネル表示に使う名前を設定（GameObject の表示名も揃える）
        void SetLabel(const std::string& label);
        const std::string& GetLabel() const { return label_; }

        /// @brief 非接触時の色
        void SetBaseColor(const CoreEngine::Vector4& color) { baseColor_ = color; ApplyColor(); }

        /// @brief 接触中の色
        void SetHitColor(const CoreEngine::Vector4& color) { hitColor_ = color; }

        /// @brief OnCollisionEnter 内で RemoveCollider() を呼ぶ（A-2 再現用）
        void SetRemoveColliderOnEnter(bool enable) { removeColliderOnEnter_ = enable; }

        // ===== 集計 =====

        ProbeStats&       Stats()       { return stats_; }
        const ProbeStats& Stats() const { return stats_; }

        // ===== オーナーへのショートカット（テストコードの記述を短く保つため） =====

        /// @brief オーナーのトランスフォーム
        CoreEngine::WorldTransform& Transform() const;

        /// @brief オーナーの先頭コライダー（無ければ nullptr）
        CoreEngine::Collider* FirstCollider() const;

        /// @brief オーナーのコライダー集合
        CoreEngine::ColliderComponent& Colliders() const;

        /// @brief オーナー本体
        CoreEngine::GameObject* Object() const { return GetOwner(); }

    private:
        /// @brief 現在の接触状態に応じてマテリアル色を反映（マテリアルが無ければ何もしない）
        void ApplyColor();

        ProbeStats  stats_;
        std::string label_ = "Probe";

        CoreEngine::Vector4 baseColor_ = { 0.75f, 0.78f, 0.82f, 1.0f };
        CoreEngine::Vector4 hitColor_  = { 0.95f, 0.25f, 0.20f, 1.0f };

        bool removeColliderOnEnter_ = false;

        CoreEngine::MaterialComponent* material_ = nullptr;
    };
}
