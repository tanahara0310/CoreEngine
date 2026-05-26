#include "pch.h"
#include "ParticleRenderDataBuilder.h"
#include "Camera/ICamera.h"
#include "Particle/ParticleSystem.h" // Particle, BillboardType, ParticleRenderMode定義のため
#include <cmath>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

uint32_t ParticleRenderDataBuilder::BuildRenderData(
    const std::vector<Particle>& particles,
    const ICamera* camera,
    BillboardType billboardType,
    ParticleRenderMode renderMode,
    ParticleForGPU* instancingData,
    uint32_t maxInstances
) {
    if (!camera || !instancingData) {
        return 0;
    }

    // renderModeは将来の拡張用（現在未使用）
(void)renderMode;

    // カメラから行列を取得
    Matrix4x4 viewMatrix = camera->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
    Matrix4x4 viewProjectionMatrix = Matrix::Multiply(viewMatrix, projectionMatrix);

    // ビルボード行列を作成
    Matrix4x4 billboardMatrix = CreateBillboardMatrix(viewMatrix, billboardType);

    // GPUデータの構築
    uint32_t instanceCount = 0;
    for (const auto& particle : particles) {
        if (instanceCount >= maxInstances) break;

        // ワールド行列を計算
        Matrix4x4 worldMatrix = CalculateWorldMatrix(particle, billboardType, billboardMatrix);

        // WVP行列を計算
        Matrix4x4 worldViewProjection = Matrix::Multiply(worldMatrix, viewProjectionMatrix);

        // GPU送信データに格納
        instancingData[instanceCount].WVP = worldViewProjection;
        instancingData[instanceCount].World = worldMatrix;
        instancingData[instanceCount].color = particle.color;

        ++instanceCount;
    }

    return instanceCount;
}

Matrix4x4 ParticleRenderDataBuilder::CalculateWorldMatrix(
    const Particle& particle,
    BillboardType billboardType,
    const Matrix4x4& billboardMatrix
) {
    if (billboardType != BillboardType::None) {
        // スケールと回転のみでローカル行列を作成（位置は原点）
        Matrix4x4 scaleRotateMatrix = Matrix::MakeAffine(
            particle.transform.scale,
            particle.transform.rotate,
            Vector3{ 0.0f, 0.0f, 0.0f }  // 原点で作成
        );

        // ビルボード変換を適用（回転とスケールのみ）
        Matrix4x4 billboardedMatrix = Matrix::Multiply(scaleRotateMatrix, billboardMatrix);

        // 最後に位置を適用
        billboardedMatrix.m[3][0] = particle.transform.translate.x;
        billboardedMatrix.m[3][1] = particle.transform.translate.y;
        billboardedMatrix.m[3][2] = particle.transform.translate.z;

        return billboardedMatrix;
    } else {
        // ビルボードなし（モデルパーティクル）の場合は通常通り
        return Matrix::MakeAffine(
            particle.transform.scale,
            particle.transform.rotate,
            particle.transform.translate
        );
    }
}

Matrix4x4 ParticleRenderDataBuilder::CreateBillboardMatrix(const Matrix4x4& viewMatrix, BillboardType type) {
    // 逆行列は全ケースで共通のため、必要な場合のみ1回だけ計算する
    Matrix4x4 invView;
    bool invViewComputed = false;
    auto getInvView = [&]() -> const Matrix4x4& {
        if (!invViewComputed) {
            invView = Matrix::Inverse(viewMatrix);
            invViewComputed = true;
        }
        return invView;
    };

    switch (type) {
    case BillboardType::ViewFacing:
    {
        Matrix4x4 billboardMatrix = getInvView();
        billboardMatrix.m[3][0] = 0.0f;
        billboardMatrix.m[3][1] = 0.0f;
        billboardMatrix.m[3][2] = 0.0f;
        return billboardMatrix;
    }

    case BillboardType::YAxisOnly:
    {
        Matrix4x4 billboardMatrix = Matrix::Identity();
        const Matrix4x4& inv = getInvView();
        Vector3 cameraPos = { inv.m[3][0], inv.m[3][1], inv.m[3][2] };
        Vector3 horizontalDirection = { cameraPos.x, 0.0f, cameraPos.z };
        float horizontalLength = std::sqrt(
            horizontalDirection.x * horizontalDirection.x +
            horizontalDirection.z * horizontalDirection.z
        );

        Vector3 forward, right;
        if (horizontalLength < 0.0001f) {
            forward = { 0.0f, 0.0f, 1.0f };
            right = { 1.0f, 0.0f, 0.0f };
        } else {
            forward = {
    horizontalDirection.x / horizontalLength,
          0.0f,
horizontalDirection.z / horizontalLength
            };
            right = { -forward.z, 0.0f, forward.x };
        }

        Vector3 up = { 0.0f, 1.0f, 0.0f };
        billboardMatrix.m[0][0] = right.x;
        billboardMatrix.m[0][1] = right.y;
        billboardMatrix.m[0][2] = right.z;
        billboardMatrix.m[1][0] = up.x;
        billboardMatrix.m[1][1] = up.y;
        billboardMatrix.m[1][2] = up.z;
        billboardMatrix.m[2][0] = forward.x;
        billboardMatrix.m[2][1] = forward.y;
        billboardMatrix.m[2][2] = forward.z;

        return billboardMatrix;
    }

    case BillboardType::ScreenAligned:
    {
        Matrix4x4 billboardMatrix = Matrix::Identity();
        const Matrix4x4& inv = getInvView();
        Vector3 right = { inv.m[0][0], inv.m[0][1], inv.m[0][2] };
        Vector3 up = { inv.m[1][0], inv.m[1][1], inv.m[1][2] };
        Vector3 forward = { inv.m[2][0], inv.m[2][1], inv.m[2][2] };

        billboardMatrix.m[0][0] = right.x;
        billboardMatrix.m[0][1] = right.y;
        billboardMatrix.m[0][2] = right.z;
        billboardMatrix.m[1][0] = up.x;
        billboardMatrix.m[1][1] = up.y;
        billboardMatrix.m[1][2] = up.z;
        billboardMatrix.m[2][0] = forward.x;
        billboardMatrix.m[2][1] = forward.y;
        billboardMatrix.m[2][2] = forward.z;

        return billboardMatrix;
    }

    case BillboardType::None:
    default:
        return Matrix::Identity();
    }
}

} // namespace CoreEngine
