#include "pch.h"
#include "RayTracingSubsystem.h"
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderingTechnique/Lighting/DeferredLightingTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/VertexAnimationDeformer.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "GameObject/Component/Render/FishSchoolComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/GameObjectManager.h"
#include "Particle/ParticleSystemComponent.h"
#include "Camera/View/ViewInfo.h"
#include "Math/MathCore.h"
#include "Scene/SceneManager.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvDynamicGeometry{ "r.RT.DynamicGeometry", true,
            "揺れる植物・海草とスキニングモデルの今の形を、レイトレーシング（影・水面の反射・コースティクス）へ"
            "毎フレーム反映する（off なら静止形の BLAS を使う）" };

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

        /// @brief メッシュのインスタンスの種類の印（材質の頂点の動きで決める）
        UINT ClassifyMeshInstanceMask(const Model& model)
        {
            for (size_t i = 0; i < model.GetMaterialCount(); ++i) {
                const MaterialInstance* material = model.GetMaterial(i);
                if (!material) {
                    continue;
                }
                switch (material->GetVertexAnimation()) {
                case VertexAnimationType::Plant:
                case VertexAnimationType::Seagrass:
                    return RayTracingInstanceMask::kVegetation;
                case VertexAnimationType::Fish:
                    return RayTracingInstanceMask::kCreature;
                default:
                    break;
                }
            }
            return RayTracingInstanceMask::kSolid;
        }

        /// @brief サブメッシュ 1 つぶんの材質をヒットシェーディングのサブメッシュ表の行へ詰める
        /// @param material          そのスロットの材質（無ければ既定値）
        /// @param baseColorOverride 全サブメッシュのベースカラーを差し替えるテクスチャ（無ければ ptr = 0）
        RTHitSubMesh MakeHitSubMesh(
            const SubMeshData& subMesh,
            const MaterialInstance* material,
            const ModelResource& resource,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorOverride,
            const DescriptorAllocator& descriptors)
        {
            RTHitSubMesh row{};
            row.firstTriangle = subMesh.startIndex / 3;
            row.triangleCount = subMesh.indexCount / 3;
            row.baseColorTextureIndex = kRTHitNoTexture;
            row.emissiveTextureIndex = kRTHitNoTexture;
            row.metallicRoughnessTextureIndex = kRTHitNoTexture;
            row.normalTextureIndex = kRTHitNoTexture;
            row.occlusionTextureIndex = kRTHitNoTexture;
            row.occlusionStrength = 0.0f;

            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture = baseColorOverride;
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture{};
            if (subMesh.materialIndex < resource.GetMaterials().size()) {
                const ModelResource::PBRTextureHandles& textures = resource.GetMaterialTextures(subMesh.materialIndex);
                if (baseColorTexture.ptr == 0) {
                    baseColorTexture = textures.baseColor;
                }
                if (textures.hasEmissive && textures.emissive.ptr != 0) {
                    row.emissiveTextureIndex = descriptors.GetSRVHeapIndex(textures.emissive);
                }
                if (textures.hasMetallicRoughness && textures.metallicRoughness.ptr != 0) {
                    row.metallicRoughnessTextureIndex = descriptors.GetSRVHeapIndex(textures.metallicRoughness);
                }
                if (textures.hasNormal) {
                    normalTexture = textures.normal;
                }
                if (textures.hasOcclusion && textures.occlusion.ptr != 0) {
                    row.occlusionTextureIndex = descriptors.GetSRVHeapIndex(textures.occlusion);
                }
            }
            if (baseColorTexture.ptr != 0) {
                row.baseColorTextureIndex = descriptors.GetSRVHeapIndex(baseColorTexture);
            }

            row.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            row.uvTransformU = { 1.0f, 0.0f, 0.0f, 0.0f };
            row.uvTransformV = { 0.0f, 1.0f, 0.0f, 0.0f };
            row.roughness = 0.5f;
            row.alphaCutoff = 0.5f;
            row.flags = kRTHitSubMeshFlagLit;
            if (material) {
                // HLSL 側は mul(float4(uv, 0, 1), uvTransform)
                const Matrix4x4 uvTransform = material->GetUVTransform();
                row.baseColor = material->GetColor();
                row.uvTransformU = { uvTransform.m[0][0], uvTransform.m[1][0], uvTransform.m[3][0], 0.0f };
                row.uvTransformV = { uvTransform.m[0][1], uvTransform.m[1][1], uvTransform.m[3][1], 0.0f };
                row.emissive = material->GetEmissiveFactor();
                row.metallic = material->GetMetallic();
                row.roughness = material->GetRoughness();
                row.alphaCutoff = material->GetAlphaCutoff();
                row.occlusionStrength = material->GetOcclusionStrength();
                row.flags = (material->IsLightingEnabled() ? kRTHitSubMeshFlagLit : 0u)
                    | (material->IsDitheringEnabled() ? kRTHitSubMeshFlagDither : 0u);
                // 法線テクスチャは材質が法線マップを使うときだけ（GBuffer.PS と同じ）
                if (material->IsNormalMapEnabled() && normalTexture.ptr != 0) {
                    row.normalTextureIndex = descriptors.GetSRVHeapIndex(normalTexture);
                }
            }
            return row;
        }

        // ヒットシェーディング（Include/RayTracing/RTHitShading.hlsli）は頂点を VertexData の並びで読む
        static_assert(sizeof(VertexData) == 64 && offsetof(VertexData, texcoord) == 16
            && offsetof(VertexData, normal) == 24 && offsetof(VertexData, tangent) == 36,
            "RTHitShading.hlsli の kRTHitVertexStride / kRTHitVertex*Offset と一致させること");

        /// @brief TLAS のインスタンス 1 つぶんの行と、そのサブメッシュの行を足す
        /// @param model 材質の持ち主（無ければモデルリソースの既定の材質）
        /// @param vertexBufferIndexOverride 頂点バッファのヒープ内インデックス（UINT32_MAX = BLAS の静止形の頂点）
        void AppendHitInstance(
            const AccelerationStructureManager& asMgr,
            const AccelerationStructureManager::InstanceDesc& instance,
            const ModelResource& resource,
            const Model* model,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorOverride,
            const DescriptorAllocator& descriptors,
            std::vector<RTHitInstance>& outInstances,
            std::vector<RTHitSubMesh>& outSubMeshes,
            uint32_t vertexBufferIndexOverride = UINT32_MAX)
        {
            RTHitInstance row{};
            for (int r = 0; r < 3; ++r) {
                row.objectToWorld[r] = {
                    instance.transform[r][0], instance.transform[r][1],
                    instance.transform[r][2], instance.transform[r][3] };
            }
            // 動く形（スキニング・揺れる植物）は、BLAS と同じ変形後の頂点を読む
            row.vertexBufferIndex = (vertexBufferIndexOverride != UINT32_MAX)
                ? vertexBufferIndexOverride
                : asMgr.GetBLASVertexBufferIndex(instance.blasIndex);
            row.indexBufferIndex = asMgr.GetBLASIndexBufferIndex(instance.blasIndex);
            row.firstSubMesh = static_cast<uint32_t>(outSubMeshes.size());

            for (const SubMeshData& subMesh : resource.GetSubMeshes()) {
                const MaterialInstance* material = model
                    ? model->GetMaterial(subMesh.materialIndex)
                    : resource.GetDefaultMaterial(subMesh.materialIndex);
                outSubMeshes.push_back(MakeHitSubMesh(subMesh, material, resource, baseColorOverride, descriptors));
            }
            if (outSubMeshes.size() == row.firstSubMesh) {
                // サブメッシュを持たないモデルは全三角形を 1 行で覆う
                SubMeshData whole{};
                whole.startIndex = 0;
                whole.indexCount = resource.GetIndexCount();
                whole.materialIndex = 0;
                outSubMeshes.push_back(MakeHitSubMesh(
                    whole, resource.GetDefaultMaterial(0), resource, baseColorOverride, descriptors));
            }
            row.subMeshCount = static_cast<uint32_t>(outSubMeshes.size()) - row.firstSubMesh;
            outInstances.push_back(row);
        }
    }

    void RayTracingSubsystem::BuildAccelerationStructures(
        const RenderContext& context,
        GraphicsCore* dx,
        ModelManager* modelManager,
        SceneManager* sceneManager)
    {
        auto* asMgr = context.accelerationStructureManager;
        if (!asMgr || !asMgr->IsSupported() || !dx) {
            return;
        }

        // フレーム開始時に RT シャドウの状態をリセット
        if (context.rtShadowManager) {
            context.rtShadowManager->ResetFrameState();
        }

        // BLAS 遅延ビルド（未構築のモデルリソース全てを対象）
        if (modelManager) {
            auto* cmdListForBLAS = dx->GetCommandList();
            modelManager->ForEachResource([&](ModelResource* resource) {
                asMgr->BuildBLASFromModelResource(cmdListForBLAS, resource);
                });
        }

        // DXR TLAS 構築（シーン内のメッシュを持つコンポーネントからインスタンスを収集）
        if (!sceneManager) {
            return;
        }

        auto* objMgr = sceneManager->GetCurrentGameObjectManager();
        if (!objMgr) {
            return;
        }

        std::vector<AccelerationStructureManager::InstanceDesc> tlasInstances;

        // ヒットシェーディングの表（TLAS のインスタンスと同じ並び）
        std::vector<RTHitInstance> hitInstances;
        std::vector<RTHitSubMesh> hitSubMeshes;
        const DescriptorAllocator* descriptors = dx->GetDescriptorAllocator();

        // ===== 動く形（スキニング・揺れる植物）は持ち主ごとの BLAS を毎フレーム更新する =====
        // 静止形の BLAS のままだと、影が揺れに付いてこないうえ、揺れた面から出たレイが
        // 動く前の自分の面に当たって縞状の影（セルフシャドウ）が出る。
        // 変形は描画と同じ時間・同じ式なので、今フレームの画面の形と一致する。
        // 水面の映り込みのヒットシェーディングにも変形後の頂点（VertexData の並び）を読ませるので、
        // 当たった点の位置・法線（そこから撃つ影のレイ）も画面の形と一致する。
        ID3D12GraphicsCommandList* asCmdList = dx->GetCommandList();
        const uint64_t frame = context.frameNumber;
        const bool dynamicGeometry = cvDynamicGeometry.Get();
        VertexAnimationDeformer* deformer = context.vertexAnimationDeformer;
        const VertexAnimationParams animParams = VertexAnimationParams::Build();
        const bool swaying = dynamicGeometry && deformer;

        /// 動く形の BLAS と、ヒットシェーディングが読む頂点バッファ（UINT32_MAX = 静止形の頂点を読む）
        struct DynamicGeometry {
            D3D12_GPU_VIRTUAL_ADDRESS blas = 0;
            uint32_t vertexBufferIndex = UINT32_MAX;
        };
        auto buildDynamicBLAS = [&](const void* owner, Model& model, const Matrix4x4& world) -> DynamicGeometry {
            ModelResource* resource = model.GetModelResource();
            AccelerationStructureManager::BLASDesc desc{};
            desc.vertexCount = resource->GetVertexCount();
            desc.vertexStride = sizeof(VertexData);  // スキニングの出力も頂点変形の出力も VertexData の並び（位置が先頭）
            desc.vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            desc.indexBuffer = resource->GetIndexBuffer();
            desc.indexCount = resource->GetIndexCount();  // 静止形の BLAS と同じく LOD0 全体
            desc.indexFormat = DXGI_FORMAT_R32_UINT;
            bool changed = true;
            uint32_t vertexBufferIndex = UINT32_MAX;
            if (model.HasSkinCluster()) {
                desc.vertexBuffer = model.PrepareSkinnedVerticesForRayTracing(asCmdList, changed);
                vertexBufferIndex = model.GetSkinnedVertexBufferHeapIndex();
            } else if (swaying && SwaysWithWindOrWater(model, animParams)) {
                const VertexAnimationDeformer::Output deformed =
                    deformer->Deform(asCmdList, owner, model, world, animParams, frame);
                desc.vertexBuffer = deformed.vertices;
                vertexBufferIndex = deformed.vertexBufferIndex;
            }
            if (!desc.vertexBuffer) {
                return {};
            }
            const D3D12_GPU_VIRTUAL_ADDRESS blas = asMgr->BuildOrUpdateDynamicBLAS(asCmdList, owner, desc, frame, changed);
            return blas ? DynamicGeometry{ blas, vertexBufferIndex } : DynamicGeometry{};
        };

        // 「メッシュを持つか」はコンポーネントの有無で決まるので、具象クラスを知る必要はない。
        // 非アクティブ／削除マーク済みのスキップは ForEachComponent が行う。
        objMgr->ForEachComponent<MeshRendererComponent>(
            [&](MeshRendererComponent& renderer) {
                // 半透明オブジェクト（水面など）は RT シャドウのキャスターから除外する
                if (renderer.GetBlendMode() != BlendMode::kBlendModeNone) return;

                // 材質は const の取得口で読む（書き込み用の取得口は材質を複製する）。
                // 書き込み用のモデルは、スキニングを今フレームぶん済ませるためだけに使う
                Model* mutableModel = renderer.GetModel();
                const Model* model = std::as_const(renderer).GetModel();
                if (!model || !mutableModel) return;

                const ModelResource* resource = model->GetModelResource();
                if (!resource || !resource->HasBLAS()) return;

                auto* transform = renderer.GetTransformComponent();
                if (!transform) return;

                const Matrix4x4& world = transform->Get().GetWorldMatrix();
                AccelerationStructureManager::InstanceDesc inst;
                inst.blasIndex = resource->GetBLASIndex();
                inst.SetTransform(world);
                inst.instanceMask = ClassifyMeshInstanceMask(*model);
                uint32_t hitVertexBufferIndex = UINT32_MAX;
                if (dynamicGeometry) {
                    // 作れなければ 0 のまま（静止形の BLAS を使う）
                    const DynamicGeometry dynamic = buildDynamicBLAS(&renderer, *mutableModel, world);
                    inst.blasAddress = dynamic.blas;
                    hitVertexBufferIndex = dynamic.vertexBufferIndex;
                }
                tlasInstances.push_back(inst);
                if (descriptors) {
                    AppendHitInstance(*asMgr, inst, *resource, model, renderer.GetTextureOverrideHandle(),
                        *descriptors, hitInstances, hitSubMeshes, hitVertexBufferIndex);
                }
            });

        // ===== モデルの粒もキャスターとして載せる =====
        // BLAS はモデル単位で構築済みなので、ここで足すのはインスタンスだけ。
        std::vector<Matrix4x4> particleMatrices;
        objMgr->ForEachComponent<ParticleSystemComponent>(
            [&](ParticleSystemComponent& particleSystem) {
                // 加算・半透明の粒が真っ黒な影を落とすと不自然なので、不透明のものだけ。
                // メッシュ側の除外条件と揃えている。
                if (particleSystem.GetBlendMode() != BlendMode::kBlendModeNone) return;

                auto* resource = particleSystem.GetModelResource();
                if (!resource || !resource->HasBLAS()) return;

                const uint32_t particleCount = particleSystem.GetParticleCount();
                if (particleCount == 0) return;

                particleMatrices.resize(particleCount);
                const uint32_t written =
                    particleSystem.CollectWorldMatrices(particleMatrices.data(), particleCount);

                for (uint32_t i = 0; i < written; ++i) {
                    AccelerationStructureManager::InstanceDesc inst;
                    inst.blasIndex = resource->GetBLASIndex();
                    inst.SetTransform(particleMatrices[i]);
                    inst.instanceMask = RayTracingInstanceMask::kParticle;
                    tlasInstances.push_back(inst);
                    if (descriptors) {
                        AppendHitInstance(*asMgr, inst, *resource, nullptr, D3D12_GPU_DESCRIPTOR_HANDLE{},
                            *descriptors, hitInstances, hitSubMeshes);
                    }
                }
            });

        // ===== 魚の群れも 1 匹ずつキャスターとして載せる =====
        // BLAS は種類（モデル）ごとに共有する。体のくねりは 1 cm ほどなので静止形で足りる。
        objMgr->ForEachComponent<FishSchoolComponent>(
            [&](FishSchoolComponent& school) {
                school.ForEachRayTracingInstance([&](const Model& fishModel, const Matrix4x4& world) {
                    const ModelResource* resource = fishModel.GetModelResource();
                    if (!resource || !resource->HasBLAS()) return;

                    AccelerationStructureManager::InstanceDesc inst;
                    inst.blasIndex = resource->GetBLASIndex();
                    inst.SetTransform(world);
                    inst.instanceMask = RayTracingInstanceMask::kCreature;
                    tlasInstances.push_back(inst);
                    if (descriptors) {
                        AppendHitInstance(*asMgr, inst, *resource, &fishModel, D3D12_GPU_DESCRIPTOR_HANDLE{},
                            *descriptors, hitInstances, hitSubMeshes);
                    }
                });
            });

        if (!tlasInstances.empty()) {
            asMgr->BuildTLAS(dx->GetCommandList(), tlasInstances);
            asMgr->UploadHitShadingTables(hitInstances, hitSubMeshes);
        }

        // 消えた・動かなくなった持ち主の動的 BLAS と頂点バッファを捨てる（GPU が使い終わってから解放）
        asMgr->RetireUnusedDynamicBLAS(frame);
        if (deformer) {
            deformer->RetireUnused(frame);
        }
    }

    bool RayTracingSubsystem::BuildShadowStageContext(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        ShadowStageContext& outStageContext)
    {
        auto* rtShadow = context.rtShadowManager;
        if (!rtShadow || !rtShadow->IsInitialized()) return false;
        if (!context.gBufferManager || !context.lightManager || !context.sceneManager) return false;
        if (!dx || !cmdList) return false;

        // 実行中のビューの ViewInfo を使う。ReflectionView を復活させる場合は
        // FrameViews へそのビューを 1 つ足せば、ここは自動的に正しい行列を引く。
        if (!context.frameViews) return false;
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!view.isValid) return false;

        outStageContext.rtShadow = rtShadow;

        // WorldPosition ターゲット廃止に伴い、深度から復元する（ビュー別に差し替わる FrameBlackboard 経由）
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneDepth, outStageContext.sceneDepthSRV);
        }
        outStageContext.projection = view.projection;
        outStageContext.invViewProj = view.invViewProjection;
        outStageContext.normalSRV =
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
        outStageContext.motionVectorSRV =
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);
        outStageContext.width = static_cast<UINT>(dx->GetClientWidth());
        outStageContext.height = static_cast<UINT>(dx->GetClientHeight());
        return true;
    }

    void RayTracingSubsystem::ForEachShadowCastingLight(
        const RenderContext& context,
        const std::function<void(uint32_t lightIndex, const Light& light)>& body)
    {
        const uint32_t maxLights = RayTracingShadowManager::kMaxDirectionalLights;
        for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
            auto* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;
            body(li, *dirLight);
        }
    }

    void RayTracingSubsystem::DispatchRTShadowTrace(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId,
        const RayTracingShadowWaterSurface& waterSurface)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
        // 水中の直接光を RT コースティクスが置き換えるのはメインライト（0 番）だけなので、
        // 水面を渡すのも 0 番だけ
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light& light) {
            stage.rtShadow->Dispatch(
                cmdList,
                stage.sceneDepthSRV,
                stage.normalSRV,
                light.direction,
                stage.invViewProj,
                stage.width,
                stage.height,
                viewId,
                li,
                (li == 0) ? waterSurface : RayTracingShadowWaterSurface{});
            });

        // GBuffer 入力の前後状態遷移は RenderGraph 側の自動遷移へ委譲する。
    }

    void RayTracingSubsystem::DispatchRTShadowTemporal(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 テンポラル蓄積パス（空間前処理+再投影+Variance Clamping）
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->ApplyTemporal(
                cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.motionVectorSRV,
                stage.projection,
                viewId,
                li);
            });
    }

    void RayTracingSubsystem::DispatchRTShadowDenoise(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 A-Trous デノイズをまとめて実行
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->Denoise(
                cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.projection,
                viewId,
                li);
            });

        // RT シャドウの全ステージが終わったので、デバッグパネルが中間バッファ
        // （生マスク・履歴）を ImGui で表示できる状態へ整える。
        // パネルが要求していないフレームは何もしない。
        stage.rtShadow->PrepareDebugViews(cmdList);
    }

    bool RayTracingSubsystem::BuildWaterDispatchContext(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        const WaterSurfaceData& surfaceData,
        const char* debugLabel,
        bool requireSceneColor,
        WaterDispatchContext& outDispatchContext)
    {
        if (!context.gBufferManager || !context.sceneManager || !dx || !cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: {} dispatch skipped. gBuffer={} sceneManager={} dx={} cmdList={}",
                debugLabel,
                context.gBufferManager != nullptr,
                context.sceneManager != nullptr,
                dx != nullptr,
                cmdList != nullptr);
            return false;
        }

        if (!context.frameViews || !context.frameViews->Get(context.viewSettings.viewType).isValid) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: {} dispatch skipped. view is invalid.",
                debugLabel);
            return false;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);

        // 深度は WorldPosition ターゲット廃止に伴いここから復元する
        // （ビュー別に差し替わるので FrameBlackboard 経由で取る）
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneDepth, outDispatchContext.sceneDepthSRV);

            // カラーソースは水面合成前の SceneColor スナップショット
            // （空・雲・ゴッドレイ・島すべてを含み、水面のみ未合成）。
            if (!context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneColorSnapshot, outDispatchContext.sceneColorSRV)) {
                context.frameBlackboard->TryGetSrvHandle(
                    FrameBlackboard::SceneColor, outDispatchContext.sceneColorSRV);
            }
        }

        if (requireSceneColor && outDispatchContext.sceneColorSRV.ptr == 0) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "RayTracingSubsystem: {} dispatch skipped. SceneColorSnapshot/SceneColor SRV is invalid.",
                debugLabel);
            return false;
        }

        outDispatchContext.viewProjection = view.viewProjection;
        outDispatchContext.cameraPosition = view.position;
        outDispatchContext.width = static_cast<UINT>(dx->GetClientWidth());
        outDispatchContext.height = static_cast<UINT>(dx->GetClientHeight());

        // FFT Ocean 経路のときだけ波面テクスチャを接続する。
        // Gerstner 経路では enabled=0 のままにして、シェーダー側を解析評価へ倒す。
        if (context.fftOceanManager
            && context.fftOceanManager->IsInitialized()
            && surfaceData.simulationType == kWaterSurfaceModelTypeFFTOcean) {
            const FFTOceanManager::Settings& fftSettings = context.fftOceanManager->GetSettings();
            outDispatchContext.fftOceanInput.displacementSRV = context.fftOceanManager->GetDisplacementSRVHandle();
            outDispatchContext.fftOceanInput.normalSRV = context.fftOceanManager->GetNormalSRVHandle();
            outDispatchContext.fftOceanInput.resolution = fftSettings.resolution;
            outDispatchContext.fftOceanInput.enabled = 1;
            static_assert(FFTOceanManager::kCascadeCount == 3, "cascadeMeanSquareSlope のカスケード数と一致させる");
            for (uint32_t c = 0; c < FFTOceanManager::kCascadeCount; ++c) {
                outDispatchContext.fftOceanInput.cascadeMeanSquareSlope[c] =
                    context.fftOceanManager->GetCascadeMeanSquareSlope(c);
            }
            const std::array<float, 3> waveGroupPhase =
                context.fftOceanManager->ComputeWaveGroupPhase(context.fftOceanSimulationTime);
            for (size_t i = 0; i < waveGroupPhase.size(); ++i) {
                outDispatchContext.fftOceanInput.waveGroupPhase[i] = waveGroupPhase[i];
            }
        }

        return true;
    }

    void RayTracingSubsystem::DispatchWaterRefraction(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterRefractionRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterRefraction = context.rtWaterRefractionManager;
        if (!rtWaterRefraction || !rtWaterRefraction->IsInitialized()) {
            return;
        }

        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water refraction", true, dispatchContext)) {
            return;
        }

        // 水中の点の照らし方は、このフレームの DeferredLighting が水中の点に使った値にそろえる
        WaterUnderwaterLightingInput underwaterLighting{};
        if (context.renderingTechniqueManager && context.rtWaterCausticsManager) {
            if (auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
                    RenderingTechniqueNames::DeferredLighting)) {
                const DeferredLightingTechnique::WaterCausticsDebugSettings& waterLighting =
                    deferredLighting->GetWaterCausticsDebugSettings();
                underwaterLighting.enabled = (waterLighting.waterVolumeEnabled != 0);
                for (int c = 0; c < 3; ++c) {
                    underwaterLighting.absorptionCoeff[c] = waterLighting.absorptionCoeff[c];
                }
                underwaterLighting.causticsIntensityScale =
                    context.rtWaterCausticsManager->GetSettings().intensityScale;
            }
        }

        rtWaterRefraction->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            BuildWaterHitShadingInput(context, dx),
            underwaterLighting,
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterReflection(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterReflectionRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterReflection = context.rtWaterReflectionManager;
        if (!rtWaterReflection || !rtWaterReflection->IsInitialized()) {
            return;
        }
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water reflection", true, dispatchContext)) {
            return;
        }

        // 空キューブマップ（空＋雲）を反射パスへ渡す。
        // レイが空へ抜けたときの色を「実際にトレースした向き」でこのパス内で
        // 解決するために必要（Water.PS 側で平面法線の空と混ぜると二重像になる）。
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV{};
        if (auto* atmosphere = context.atmosphereManager) {
            if (atmosphere->IsSkySpecularEnabled() && atmosphere->IsSkyEnvironmentReady()) {
                skyEnvironmentSRV = atmosphere->GetSkySpecularSRVHandle();
            }
        }

        // 水面の影はメインライト（0 番）で調べる（Water.PS が掛けるのも 0 番の項だけ）
        WaterSunShadowInput sunShadow{};
        if (context.lightManager) {
            if (Light* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                sunShadow.direction = CoreEngine::Normalize(mainLight->direction);
                sunShadow.enabled = true;
            }
        }

        rtWaterReflection->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            skyEnvironmentSRV,
            sunShadow,
            BuildWaterHitShadingInput(context, dx),
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    WaterHitShadingInput RayTracingSubsystem::BuildWaterHitShadingInput(
        const RenderContext& context, GraphicsCore* dx)
    {
        WaterHitShadingInput input{};
        const DescriptorAllocator* descriptors = dx ? dx->GetDescriptorAllocator() : nullptr;
        const AccelerationStructureManager* asMgr = context.accelerationStructureManager;
        if (!descriptors || !asMgr || !asMgr->HasHitShadingTables()) {
            return input;
        }
        input.instanceTableIndex = asMgr->GetHitInstanceTableIndex();
        input.subMeshTableIndex = asMgr->GetHitSubMeshTableIndex();
        input.enabled = (input.instanceTableIndex != UINT32_MAX && input.subMeshTableIndex != UINT32_MAX);

        // DeferredLighting と同じライトの配列（0 番がメインライト）
        if (context.lightManager) {
            input.directionalLightsIndex =
                descriptors->GetSRVHeapIndex(context.lightManager->GetDirectionalLightsSRVHandle());
            if (input.directionalLightsIndex != UINT32_MAX) {
                input.directionalLightCount = (std::min)(
                    context.lightManager->GetLightCount(LightType::Directional),
                    LightManager::GetMaxLightCount(LightType::Directional));
            }
        }

        // 空の光（DeferredLighting と同じ有効条件）
        if (auto* atmosphere = context.atmosphereManager) {
            const bool skyAmbientUsable = atmosphere->IsAtmosphereActive()
                && atmosphere->IsSkyAmbientEnabled()
                && atmosphere->IsSkyAmbientReady()
                && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0;
            if (skyAmbientUsable) {
                input.skyIrradianceSHIndex = descriptors->GetSRVHeapIndex(atmosphere->GetSkyIrradianceSHSRVHandle());
                input.skyAmbientEnabled = (input.skyIrradianceSHIndex != UINT32_MAX);
                input.skyAmbientScale = atmosphere->GetSkyAmbientScale();

                const bool skySpecularUsable = atmosphere->IsSkySpecularEnabled()
                    && atmosphere->IsSkyEnvironmentReady()
                    && atmosphere->GetSkySpecularSRVHandle().ptr != 0;
                if (skySpecularUsable) {
                    input.skySpecularMapIndex = descriptors->GetSRVHeapIndex(atmosphere->GetSkySpecularSRVHandle());
                    input.skySpecularEnabled = (input.skySpecularMapIndex != UINT32_MAX);
                }
            }
        }

        // 雲の影（DeferredLighting と同じマップと範囲）
        if (auto* clouds = context.volumetricCloudManager; clouds && clouds->AreCloudsActive() && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE cloudShadowMap{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::CloudShadowMap, cloudShadowMap)) {
                const CloudShadowShaderConstants& cloudShadow = clouds->GetCloudShadowConstants();
                input.cloudShadowMapIndex = descriptors->GetSRVHeapIndex(cloudShadowMap);
                input.cloudShadowStrength = cloudShadow.sceneStrength;
                input.cloudShadowRegionCenterXZ[0] = cloudShadow.regionCenterX;
                input.cloudShadowRegionCenterXZ[1] = cloudShadow.regionCenterZ;
                input.cloudShadowRegionSize = cloudShadow.regionSizeM;
                input.cloudShadowAnchorY = cloudShadow.anchorWorldY;
                input.cloudShadowEdgeFadeStart = cloudShadow.edgeFadeStart;
            }
        }
        return input;
    }

    void RayTracingSubsystem::DispatchWaterCaustics(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterCausticsRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterCaustics = context.rtWaterCausticsManager;
        if (!rtWaterCaustics || !rtWaterCaustics->IsInitialized()) {
            return;
        }
        // コースティクスは屈折・反射と違い SceneColor へ再投影しないので必須ではない
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water caustics", false, dispatchContext)) {
            return;
        }

        // シーンの実際のディレクショナルライトの方向・色・強度・有効フラグを RT コースティクスへ
        // 伝える。以前は方向しか渡しておらず、ライトを消してもコースティクスが消えない、
        // ライトの色/強度を変えても常に一定の白い光量のままになる不具合の原因だった。
        WaterCausticsRayTracingManager::LightInput lightInput{};
        if (context.lightManager) {
            if (Light* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                lightInput.direction = CoreEngine::Normalize(mainLight->direction);
                // 大気透過率適用済みの実効色（DeferredLighting・SS版コースティクスと同一基準）。
                // 生の color を渡すと日没時に置換前後で輝度が食い違い水面線が不連続になる
                const Vector3 effectiveColor =
                    context.lightManager->GetEffectiveLightColorRGB(*mainLight);
                lightInput.color = { effectiveColor.x, effectiveColor.y, effectiveColor.z };
                // オーサリングは照度 [lx]。シェーダーが期待する従来単位へ換算する
                lightInput.intensity = LightUnits::LuxToShader(mainLight->intensity);
                lightInput.enabled = true;
            } else {
                lightInput.enabled = false;
            }
        } else {
            lightInput.enabled = false;
        }

        rtWaterCaustics->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness),
            lightInput,
            surfaceData,
            dispatchContext.fftOceanInput,
            MathCore::Matrix::Inverse(dispatchContext.viewProjection),
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterSeabed(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterSeabedRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterSeabed = context.rtWaterSeabedManager;
        if (!rtWaterSeabed || !rtWaterSeabed->IsInitialized()) {
            return;
        }
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water seabed", false, dispatchContext)) {
            return;
        }

        // Gerstner 経路では変位テクスチャを読まないが、宣言されたスロットには何かを差す
        WaterRayTracingPassBase::FFTOceanInput fftOceanInput = dispatchContext.fftOceanInput;
        if (fftOceanInput.displacementSRV.ptr == 0) {
            fftOceanInput.displacementSRV = dispatchContext.sceneDepthSRV;
        }

        rtWaterSeabed->Dispatch(cmdList, surfaceData, fftOceanInput, viewId);
    }
}
