#include "pch.h"
#include "WorldTransform.h"

#include <cmath>

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

Vector3 ScaleOfWorldMatrix(const Matrix4x4& world)
{
    const auto& m = world.m;
    auto axisLength = [&m](int row) {
        return std::sqrt(m[row][0] * m[row][0] + m[row][1] * m[row][1] + m[row][2] * m[row][2]);
        };
    return { axisLength(0), axisLength(1), axisLength(2) };
}

void AxesOfWorldMatrix(const Matrix4x4& world, Vector3& axisX, Vector3& axisY, Vector3& axisZ)
{
    axisX = { 1.0f, 0.0f, 0.0f };
    axisY = { 0.0f, 1.0f, 0.0f };
    axisZ = { 0.0f, 0.0f, 1.0f };

    Vector3* const axes[3] = { &axisX, &axisY, &axisZ };
    for (int row = 0; row < 3; ++row) {
        const Vector3 basis{ world.m[row][0], world.m[row][1], world.m[row][2] };
        if (LengthSquared(basis) > 1e-12f) {
            *axes[row] = Normalize(basis);
        }
    }
}
}
