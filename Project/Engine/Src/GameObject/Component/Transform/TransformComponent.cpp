#include "pch.h"
#include "TransformComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <cmath>

REFLECT_REGISTER(CoreEngine::TransformComponent)
COMPONENT_REGISTER(CoreEngine::TransformComponent)

namespace CoreEngine
{
    void TransformComponent::Awake()
    {
        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        if (auto* dxCommon = engine ? engine->GetService<GraphicsCore>() : nullptr) {
            transform_.Initialize(dxCommon->GetDevice());
        }
    }

    Vector3 TransformComponent::GetWorldScale() const
    {
        // 行ベクトル規約（p' = p * M）なので、各行が基底ベクトル。その長さがスケール。
        const auto& m = transform_.GetWorldMatrix().m;
        auto axisLength = [&m](int row) {
            return std::sqrt(m[row][0] * m[row][0] + m[row][1] * m[row][1] + m[row][2] * m[row][2]);
            };
        return { axisLength(0), axisLength(1), axisLength(2) };
    }

    bool TransformComponent::ApplyWorldDelta(const Vector3& delta)
    {
        if (const WorldTransform* parent = transform_.GetParent()) {
            // 親がいる場合、delta はワールド量なので親のローカル空間へ落としてから足す。
            // delta は「向きと大きさ」を持つベクトルなので平行移動成分は掛けない
            // （TransformNormal が w=0 として扱う）。
            const Matrix4x4 parentInverse =
                MathCore::Matrix::Inverse(parent->GetWorldMatrix());
            transform_.translate =
                transform_.translate
                + MathCore::CoordinateTransform::TransformNormal(delta, parentInverse);
        } else {
            transform_.translate = transform_.translate + delta;
        }

        // 同一フレーム内の後続ペアが新しい位置で判定されるようワールド行列を更新する
        SyncWorldMatrix();
        return true;
    }

    void TransformComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
        // ギズモ・当たり判定・描画が同じフレームで新しい値を見られるようにする
        SyncWorldMatrix();
    }

    void TransformComponent::SyncWorldMatrix()
    {
        ApplyParent();
        transform_.TransferMatrix();
    }

    bool TransformComponent::SetParent(TransformComponent* parent)
    {
        if (parent && IsSelfOrDescendant(parent)) {
            const GameObject* owner = GetOwner();
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "Transform: \"{}\" の親に、自分自身か自分の子孫は指定できません",
                owner ? owner->GetName() : std::string());
            return false;
        }
        parent_.Set(parent);
        SyncWorldMatrix();
        return true;
    }

    void TransformComponent::ApplyParent()
    {
        const TransformComponent* parent = parent_.Get();
        if (parent == appliedParent_) {
            return;
        }
        if (parent && IsSelfOrDescendant(parent)) {
            const GameObject* owner = GetOwner();
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "Transform: \"{}\" の親が自分自身か自分の子孫を指していたので、親を外しました",
                owner ? owner->GetName() : std::string());
            parent_.Reset();
            parent = nullptr;
        }
        appliedParent_ = parent;
        transform_.SetParent(parent ? &parent->Get() : nullptr);
    }

    bool TransformComponent::IsSelfOrDescendant(const TransformComponent* candidate) const
    {
        // candidate から親をたどって自分に着けば、candidate は自分か自分の子孫。
        // 壊れた循環があっても止まるように、たどる段数に上限を置く
        constexpr int kMaxDepth = 256;
        const TransformComponent* node = candidate;
        for (int depth = 0; node && depth < kMaxDepth; ++depth) {
            if (node == this) {
                return true;
            }
            node = node->parent_.Get();
        }
        return false;
    }
}
