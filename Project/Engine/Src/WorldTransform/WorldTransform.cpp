#include "pch.h"
#include "WorldTransform.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include <cassert>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

void WorldTransform::Initialize(ID3D12Device* device)
{
    // 定数バッファを作成
    constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(ConstantBufferDataWorldTransform));

    // マッピング
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped_));
    assert(mapped_ != nullptr);

    // 初期行列を転送
    TransferMatrix();
}

void WorldTransform::TransferMatrix()
{
    // ローカル行列を計算
    const Matrix4x4 localMatrix = Matrix::MakeAffine(scale_, rotation_, translate_);

    // 親がいる場合は親の行列と合成
    if (parent_) {
        matWorld_ = localMatrix * parent_->GetWorldMatrix();
    } else {
        matWorld_ = localMatrix;
    }

    // GPUに転送
    if (mapped_) {
        mapped_->matWorld = matWorld_;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS WorldTransform::GetGPUVirtualAddress() const
{
    return constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
}

Vector3 WorldTransform::GetWorldPosition() const
{
    return { matWorld_.m[3][0], matWorld_.m[3][1], matWorld_.m[3][2] };
}

void WorldTransform::SetWorldMatrix(const Matrix4x4& matrix)
{
    matWorld_ = matrix;
    
    // GPUに転送
    if (mapped_) {
        mapped_->matWorld = matWorld_;
    }
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
