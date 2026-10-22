#include "pch.h"
#include "WorldTransform.h"

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

void WorldTransform::Initialize()
{
    UpdateWorldMatrix();
}

void WorldTransform::UpdateWorldMatrix()
{
    // ローカル行列を計算
    const Matrix4x4 localMatrix = Matrix::MakeAffine(scale_, rotation_, translate_);

    // 親がいる場合は親の行列と合成
    if (parent_) {
        matWorld_ = localMatrix * parent_->GetWorldMatrix();
    } else {
        matWorld_ = localMatrix;
    }
}

Vector3 WorldTransform::GetWorldPosition() const
{
    return { matWorld_.m[3][0], matWorld_.m[3][1], matWorld_.m[3][2] };
}

void WorldTransform::SetWorldMatrix(const Matrix4x4& matrix)
{
    matWorld_ = matrix;
}

void WorldTransform::SetRotation(const Quaternion& rotation)
{
    rotation_ = rotation;
    rotationEuler_ = QuaternionMath::ToEuler(rotation);
}

void WorldTransform::SetRotationEuler(const Vector3& radians)
{
    rotationEuler_ = radians;
    rotation_ = QuaternionMath::MakeRotateEuler(radians);
}
}
