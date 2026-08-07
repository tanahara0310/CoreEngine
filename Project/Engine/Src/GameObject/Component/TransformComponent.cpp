#include "pch.h"
#include "TransformComponent.h"

#include "Math/MathCore.h"

#include <cmath>

namespace CoreEngine
{
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
        transform_.TransferMatrix();
        return true;
    }
}
