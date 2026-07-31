#include "pch.h"
#include "ModelVisibility.h"

#include "Graphics/Render/Culling/HiZOcclusionSystem.h"
#include "Graphics/Render/RenderOptimizationSettings.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Common/EngineStats.h"
#include "Camera/ICamera.h"
#include "Math/BoundingBox.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace CoreEngine
{
    namespace
    {
        /// @brief AABB の画面投影サイズから LOD レベルを算出する（0=フル詳細）
        uint32_t ComputeLodByCoverage(const BoundingBox& localAABB,
            const Matrix4x4& worldMatrix, const ICamera* camera)
        {
            // 画面占有率のしきい値。coverage は「モデルの外接球半径が画面半分の高さに
            // 対して占める割合」で、1.0 なら画面の半分を覆う大きさ。
            // マイクロトライアングルのラスタライズがGBufferの支配項のため、閾値を上げて
            // 近〜中距離でも早めにLOD1/2へ落とし三角形数を減らす（2026-07-22チューニング）。
            constexpr float kLod1CoverageThreshold = 0.75f; // これ未満で LOD1（詳細 25%）
            constexpr float kLod2CoverageThreshold = 0.35f; // これ未満で LOD2（詳細 6%）

            if (!localAABB.IsValid() || !camera) {
                return 0;
            }

            const auto& m = worldMatrix.m;

            // ローカルAABBの中心と外接球半径
            const Vector3 localCenter = {
                (localAABB.min.x + localAABB.max.x) * 0.5f,
                (localAABB.min.y + localAABB.max.y) * 0.5f,
                (localAABB.min.z + localAABB.max.z) * 0.5f,
            };
            const float ex = (localAABB.max.x - localAABB.min.x) * 0.5f;
            const float ey = (localAABB.max.y - localAABB.min.y) * 0.5f;
            const float ez = (localAABB.max.z - localAABB.min.z) * 0.5f;
            const float localRadius = std::sqrt(ex * ex + ey * ey + ez * ez);

            // 中心をワールドへ変換（行ベクトル規約: p' = p * M）
            const Vector3 worldCenter = {
                localCenter.x * m[0][0] + localCenter.y * m[1][0] + localCenter.z * m[2][0] + m[3][0],
                localCenter.x * m[0][1] + localCenter.y * m[1][1] + localCenter.z * m[2][1] + m[3][1],
                localCenter.x * m[0][2] + localCenter.y * m[1][2] + localCenter.z * m[2][2] + m[3][2],
            };

            // ワールド行列の各軸スケール（行ベクトルの長さ）の最大値で半径を拡大
            const float scaleX = std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]);
            const float scaleY = std::sqrt(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]);
            const float scaleZ = std::sqrt(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]);
            const float worldRadius = localRadius * (std::max)({ scaleX, scaleY, scaleZ });

            const Vector3 cameraPosition = camera->GetPosition();
            const float dx = worldCenter.x - cameraPosition.x;
            const float dy = worldCenter.y - cameraPosition.y;
            const float dz = worldCenter.z - cameraPosition.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // カメラが外接球の内側にいる場合は常にフル詳細
            if (distance <= worldRadius) {
                return 0;
            }

            // 射影行列の m[1][1] = cot(fovY/2)。coverage = 半径の NDC 高さ（半画面=1.0）
            const float cotHalfFovY = camera->GetProjectionMatrix().m[1][1];
            const float coverage = (worldRadius / distance) * cotHalfFovY;

            if (coverage >= kLod1CoverageThreshold) {
                return 0;
            }
            if (coverage >= kLod2CoverageThreshold) {
                return 1;
            }
            return 2;
        }
    }

    ModelVisibility::~ModelVisibility()
    {
        if (hiZ_) {
            for (uint32_t id : occlusionIds_) {
                hiZ_->UnregisterTarget(id);
            }
        }
        occlusionIds_.clear();
    }

    uint32_t ModelVisibility::SelectLod(const ModelResource& resource, const Matrix4x4& worldMatrix,
        const ICamera* camera)
    {
        const auto& settings = RenderOptimizationSettings::Get();
        if (settings.forcedLodIndex >= 0) {
            // デバッグ用の強制LOD（A/B計測用に固定する）
            return static_cast<uint32_t>(settings.forcedLodIndex);
        }
        if (!settings.lodEnabled) {
            return 0;
        }
        return ComputeLodByCoverage(resource.GetLocalBoundingBox(), worldMatrix, camera);
    }

    bool ModelVisibility::IsModelInView(const Frustum& frustum, const BoundingBox& worldBounds)
    {
        if (!RenderOptimizationSettings::Get().frustumCullingEnabled) {
            return true;
        }
        if (!worldBounds.IsValid()) {
            return true;
        }
        return !frustum.IsOutside(worldBounds);
    }

    void ModelVisibility::BeginOcclusionQuery(HiZOcclusionSystem* hiZ,
        const ModelResource& resource, const Matrix4x4& worldMatrix,
        const Matrix4x4& viewProjection, bool eligible)
    {
        // 登録先システムはモデルの生存中は不変の前提（スロット返却先が変わると二重管理になる）。
        // 登録済みスロットの返却先を失わないよう、null では上書きしない
        if (hiZ) {
            hiZ_ = hiZ;
        }
        active_ = hiZ && eligible && hiZ->IsCollectEnabled();
        if (!active_) {
            return;
        }

        resource_ = &resource;
        worldMatrix_ = worldMatrix;
        hiZ_->SetViewProjection(viewProjection);

        if (occlusionIds_.size() != resource.GetSubMeshes().size()) {
            // サブメッシュ数が変わった場合（リロード等）は登録し直す
            for (uint32_t id : occlusionIds_) {
                hiZ_->UnregisterTarget(id);
            }
            occlusionIds_.assign(resource.GetSubMeshes().size(), HiZOcclusionSystem::kInvalidId);
        }
    }

    bool ModelVisibility::IsSubMeshVisible(uint32_t subMeshIndex)
    {
        if (!active_ || !resource_) {
            return true;
        }
        if (subMeshIndex >= occlusionIds_.size()) {
            // BeginOcclusionQuery がサブメッシュ数に同期させているため、ここに来るのは
            // 呼び出し側のインデックスずれ（バグ）のみ。Release では可視扱いで安全側に倒す
            assert(false && "IsSubMeshVisible: subMeshIndex out of range (index mismatch bug)");
            return true;
        }

        uint32_t& occlusionId = occlusionIds_[subMeshIndex];
        if (occlusionId == HiZOcclusionSystem::kInvalidId) {
            occlusionId = hiZ_->RegisterTarget();
        }

        const BoundingBox& localBounds = resource_->GetSubMeshLocalBounds(subMeshIndex);
        if (occlusionId != HiZOcclusionSystem::kInvalidId && localBounds.IsValid()) {
            hiZ_->SubmitBounds(occlusionId, localBounds.TransformBy(worldMatrix_));
            if (!hiZ_->IsVisible(occlusionId)) {
                EngineStats::GetInstance().RecordOcclusionCulled();
                return false;
            }
        }
        return true;
    }
}
