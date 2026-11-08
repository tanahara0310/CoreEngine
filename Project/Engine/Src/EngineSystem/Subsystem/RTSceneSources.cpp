#include "pch.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Material/SurfaceMaterialPipeline.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RayTracing/RTObjectSettings.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/VertexAnimationDeformer.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Particle/ParticleSystemComponent.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvDynamicGeometry{ "r.RT.DynamicGeometry", true,
            "揺れる植物・海草とスキニングモデルの今の形を、レイトレーシング（影・水面の反射・コースティクス）へ"
            "毎フレーム反映する（off なら静止形の BLAS を使う）" };

        CVar<float> cvDynamicGeometryDistance{ "r.RT.DynamicGeometry.Distance", 50.0f,
            "揺れる植物・海草の揺れをレイトレーシングへ反映する、カメラからの距離 [m]。"
            "これより遠い物は静止形の BLAS を使う（0 なら距離によらずすべて反映する）",
            CVarRange{ 0.0f, 2000.0f } };

        /// @brief 反映をやめる距離 = 反映を始める距離 × この倍率（反映中の物はこの距離まで反映を続ける）
        constexpr float kDynamicGeometryKeepScale = 1.1f;

        /// @brief カメラからモデルの外接球の表面までの距離 [m]（カメラが球の中なら 0 以下）
        float DistanceToBounds(const ModelResource& resource, const Matrix4x4& world, const Vector3& cameraPosition)
        {
            const BoundingBox& box = resource.GetLocalBoundingBox();
            if (!box.IsValid()) {
                return 0.0f;
            }
            const Vector3 localCenter = {
                (box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f };
            const Vector3 half = { (box.max.x - box.min.x) * 0.5f, (box.max.y - box.min.y) * 0.5f,
                (box.max.z - box.min.z) * 0.5f };
            const auto& m = world.m;
            // 行ベクトル規約（p' = p * M）で中心をワールドへ移し、半径は各軸の拡大の最大で広げる
            const Vector3 center = {
                localCenter.x * m[0][0] + localCenter.y * m[1][0] + localCenter.z * m[2][0] + m[3][0],
                localCenter.x * m[0][1] + localCenter.y * m[1][1] + localCenter.z * m[2][1] + m[3][1],
                localCenter.x * m[0][2] + localCenter.y * m[1][2] + localCenter.z * m[2][2] + m[3][2] };
            const float scale = (std::max)({
                std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]),
                std::sqrt(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]),
                std::sqrt(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]) });
            const float radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z) * scale;
            const float dx = center.x - cameraPosition.x;
            const float dy = center.y - cameraPosition.y;
            const float dz = center.z - cameraPosition.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
        }

        /// @brief 今フレーム風や波で揺れる（植物・海草の）マテリアルを持つか
        /// @note 魚の泳ぎは 1 cm ほどしか動かないので、影は静止形（共有の BLAS）で足りる
        bool SwaysWithWindOrWater(const Model& model, const VertexAnimationParams& params)
        {
            const ModelResource* resource = model.GetModelResource();
            if (!resource || !resource->HasVertexAnimationData()) {
                return false;
            }
            for (size_t i = 0; i < model.GetMaterialCount(); ++i) {
                const MaterialInstance* material = model.GetMaterial(i);
                if (!material || material->GetVertexAnimStrength() <= 0.0f) {
                    continue;
                }
                const VertexAnimationType type = material->GetVertexAnimation();
                if ((type == VertexAnimationType::Plant && params.windStrength > 0.0f)
                    || (type == VertexAnimationType::Seagrass && params.waterStrength > 0.0f)) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 形の決まった物か（植物・海草・魚の動きの材質を持たない）
        bool IsSolidShape(const Model& model)
        {
            for (size_t i = 0; i < model.GetMaterialCount(); ++i) {
                const MaterialInstance* material = model.GetMaterial(i);
                if (!material) {
                    continue;
                }
                switch (material->GetVertexAnimation()) {
                case VertexAnimationType::Plant:
                case VertexAnimationType::Seagrass:
                case VertexAnimationType::Fish:
                    return false;
                default:
                    break;
                }
            }
            return true;
        }

        /// @brief メッシュ描画の部品を載せる
        /// @details 動く形（スキニング・揺れる植物・頂点を動かすマテリアル）は持ち主ごとの BLAS を毎フレーム作る。
        ///          静止形の BLAS のままだと、影が揺れに付いてこないうえ、揺れた面から出たレイが
        ///          動く前の自分の面に当たって縞状の影（セルフシャドウ）が出る。
        ///          変形は描画と同じ時間・同じ式なので、今フレームの画面の形と一致する。
        ///          水面の映り込みのヒットシェーディングにも変形後の頂点を読ませるので、
        ///          当たった点の位置・法線（そこから撃つ影のレイ）も画面の形と一致する。
        ///          揺れはカメラから r.RT.DynamicGeometry.Distance 以内の物だけ反映し、それより遠い物は静止形の BLAS を使う。
        void CollectMeshRenderers(RTScene::Builder& builder)
        {
            const RTScene::FrameInput& frame = builder.GetFrame();
            if (!frame.objects) {
                return;
            }
            const bool dynamicGeometry = cvDynamicGeometry.Get();
            VertexAnimationDeformer* deformer = frame.deformer;
            const VertexAnimationParams& animParams = builder.GetVertexAnimationParams();
            const bool swaying = dynamicGeometry && deformer;
            const float swayDistance = cvDynamicGeometryDistance.Get();

            /// 揺れを反映する距離の内にあるか（反映中の物は kDynamicGeometryKeepScale 倍の距離まで反映を続ける）
            auto withinSwayDistance = [&](const void* owner, const ModelResource& resource, const Matrix4x4& world,
                                          float boundsPadding) {
                if (swayDistance <= 0.0f || !frame.hasCamera) {
                    return true;
                }
                const float limit = builder.HasDynamicShape(owner)
                    ? swayDistance * kDynamicGeometryKeepScale : swayDistance;
                return DistanceToBounds(resource, world, frame.cameraPosition) - boundsPadding <= limit;
            };

            // 非アクティブ／削除マーク済みのスキップは ForEachComponent が行う
            frame.objects->ForEachComponent<MeshRendererComponent>(
                [&](MeshRendererComponent& renderer) {
                    const RTObjectSettings& settings = renderer.GetRayTracingSettings();
                    if (!settings.enabled) return;

                    // 材質は const の取得口で読む（書き込み用の取得口は材質を複製する）。
                    // 書き込み用のモデルは、スキニングを今フレームぶん済ませるためだけに使う
                    Model* mutableModel = renderer.GetModel();
                    const Model* model = std::as_const(renderer).GetModel();
                    if (!model || !mutableModel) return;

                    const ModelResource* resource = model->GetModelResource();
                    if (!resource || !resource->IsLoaded()) return;

                    auto* transform = renderer.GetTransformComponent();
                    if (!transform) return;

                    const Matrix4x4& world = transform->Get().GetWorldMatrix();
                    const bool opaque = renderer.GetBlendMode() == BlendMode::kBlendModeNone;

                    RTScene::Instance instance;
                    instance.resource = resource;
                    instance.model = model;
                    instance.world = world;
                    instance.baseColorOverride = renderer.GetTextureOverrideHandle();
                    instance.owner = renderer.GetOwner() ? renderer.GetOwner()->GetObjectId() : ObjectId{};
                    instance.mask = settings.ResolvePurposeMask(opaque);
                    // 形の決まった不透明な物は、少し前まで動いていた物とスキニングの物を動く物の印にする
                    if (opaque && IsSolidShape(*model)) {
                        instance.mask |= (builder.TrackSolidMotion(&renderer, world) || model->HasSkinCluster())
                            ? RTInstanceMask::kMovingSolid : RTInstanceMask::kStaticSolid;
                    }
                    const size_t row = builder.Add(instance);
                    if (row == RTScene::kNoRow || !dynamicGeometry) return;

                    RTScene::DynamicShape shape;
                    shape.owner = &renderer;
                    // 頂点を動かすマテリアル（`#define SURFACE_VERTEX`）は、揺れと同じ距離の内で同じ式の CS を掛ける
                    SurfaceMaterialPipeline* const surface = model->GetSurfacePipeline();
                    const bool surfaceMoves = deformer && surface && surface->HasVertexFunction()
                        && withinSwayDistance(&renderer, *resource, world, renderer.GetBoundsPadding());
                    if (model->HasSkinCluster()) {
                        shape.vertices = mutableModel->PrepareSkinnedVerticesForRayTracing(frame.cmdList, shape.changed);
                        shape.vertexBufferIndex = model->GetSkinnedVertexBufferHeapIndex();
                        if (!shape.vertices) return;
                        if (surfaceMoves) {
                            // スキン済みの頂点にもう 1 回掛ける（作れなければスキン済みの頂点のまま）
                            VertexAnimationDeformer::Request request{ &renderer, model, world };
                            request.sourceVertices = shape.vertices;
                            request.surface = surface;
                            request.slot = model->GetSurfaceSlot();
                            request.applyVertexAnimation = false;
                            shape.deform = request;
                        }
                        builder.SetDynamicShape(row, std::move(shape));
                    } else if (surfaceMoves || (swaying && SwaysWithWindOrWater(*model, animParams)
                        && withinSwayDistance(&renderer, *resource, world, renderer.GetBoundsPadding()))) {
                        VertexAnimationDeformer::Request request{ &renderer, model, world };
                        if (surfaceMoves) {
                            request.surface = surface;
                            request.slot = model->GetSurfaceSlot();
                        }
                        shape.deform = request;
                        builder.SetDynamicShape(row, std::move(shape));
                    }
                });
        }

        /// @brief モデルの粒を載せる（BLAS はモデル単位なので、粒ごとに足すのはインスタンスだけ）
        void CollectParticleSystems(RTScene::Builder& builder)
        {
            const RTScene::FrameInput& frame = builder.GetFrame();
            if (!frame.objects) {
                return;
            }
            std::vector<Matrix4x4> particleMatrices;
            frame.objects->ForEachComponent<ParticleSystemComponent>(
                [&](ParticleSystemComponent& particleSystem) {
                    const RTObjectSettings& settings = particleSystem.GetRayTracingSettings();
                    if (!settings.enabled) return;

                    const UINT mask =
                        settings.ResolvePurposeMask(particleSystem.GetBlendMode() == BlendMode::kBlendModeNone);
                    if (mask == 0) return;

                    const ModelResource* resource = particleSystem.GetModelResource();
                    if (!resource || !resource->IsLoaded()) return;

                    const uint32_t particleCount = particleSystem.GetParticleCount();
                    if (particleCount == 0) return;

                    particleMatrices.resize(particleCount);
                    const uint32_t written =
                        particleSystem.CollectWorldMatrices(particleMatrices.data(), particleCount);

                    RTScene::Instance instance;
                    instance.resource = resource;
                    instance.mask = mask;
                    instance.owner = particleSystem.GetOwner() ? particleSystem.GetOwner()->GetObjectId() : ObjectId{};
                    for (uint32_t i = 0; i < written; ++i) {
                        instance.world = particleMatrices[i];
                        builder.Add(instance);
                    }
                });
        }
    }
}

RT_SCENE_SOURCE_REGISTER("メッシュ描画", CoreEngine::CollectMeshRenderers)
RT_SCENE_SOURCE_REGISTER("パーティクル", CoreEngine::CollectParticleSystems)
