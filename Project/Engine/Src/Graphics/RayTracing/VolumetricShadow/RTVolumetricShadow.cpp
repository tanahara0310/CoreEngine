#include "pch.h"
#include "RTVolumetricShadow.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>

#include "Camera/View/ViewInfo.h"
#include "Graphics/Fog/Volumetric/VolumetricFogCVars.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Math/IntegerMath.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 日なたとみなす距離の余裕（画素の幅の何倍か）
        constexpr float kBiasTexels = 2.0f;

        /// @brief 前の画像を使い回してよい光の向きの差（cos）
        constexpr float kReuseMinDirectionCos = 0.99995f;

        /// @brief 前の画像を使い回してよい、霧の範囲を包む球の中心と半径のずれ（半径に対する割合）
        constexpr float kReuseMaxShiftRatio = 0.05f;

        /// @brief RTVolumetricShadow.CS.hlsl の RTVolumetricShadowConstants
        struct alignas(16) RTVolumetricShadowConstants {
            Vector3 origin;                 // offset   0
            float size;                     // offset  12
            Vector3 right;                  // offset  16
            float range;                    // offset  28
            Vector3 up;                     // offset  32
            uint32_t resolution;            // offset  44
            Vector3 direction;              // offset  48
            float texelSize;                // offset  60
            uint32_t hitInstanceTableIndex; // offset  64
            uint32_t hitSubMeshTableIndex;  // offset  68
            uint32_t pad0;                  // offset  72
            uint32_t pad1;                  // offset  76
        };
        static_assert(sizeof(RTVolumetricShadowConstants) == 80, "RTVolumetricShadowConstants と HLSL の cbuffer の大きさが違う");

        constexpr Cb::Field kRTVolumetricShadowConstantsFields[] = {
            CB_FIELD(RTVolumetricShadowConstants, origin), CB_FIELD(RTVolumetricShadowConstants, size),
            CB_FIELD(RTVolumetricShadowConstants, right), CB_FIELD(RTVolumetricShadowConstants, range),
            CB_FIELD(RTVolumetricShadowConstants, up), CB_FIELD(RTVolumetricShadowConstants, resolution),
            CB_FIELD(RTVolumetricShadowConstants, direction), CB_FIELD(RTVolumetricShadowConstants, texelSize),
            CB_FIELD(RTVolumetricShadowConstants, hitInstanceTableIndex),
            CB_FIELD(RTVolumetricShadowConstants, hitSubMeshTableIndex),
            CB_FIELD(RTVolumetricShadowConstants, pad0), CB_FIELD(RTVolumetricShadowConstants, pad1),
        };
        CB_VERIFY_LAYOUT(RTVolumetricShadowConstants, kRTVolumetricShadowConstantsFields);
        CB_BIND_HLSL(RTVolumetricShadowConstants, kRTVolumetricShadowConstantsFields, "RTVolumetricShadowConstants");

        namespace RTVolumetricShadowBind {
            enum Slot : size_t { RTVolumetricShadowConstants, gScene, gSunDepth, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "RTVolumetricShadowConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gScene",                      ShaderBindingType::SRV, BindingUsage::Required },
                { "gSunDepth",                   ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        /// @brief 霧の格子が届く範囲（カメラから fogDistance までの視錐台）を包む球を求める
        /// @return ビューの行列から向きが求まらなければ false
        bool ComputeFogBoundingSphere(const ViewInfo& view, float fogDistance, Vector3& center, float& radius)
        {
            using MathCore::CoordinateTransform::TransformCoord;
            const Vector3 toCenter = TransformCoord(Vector3{ 0.0f, 0.0f, 1.0f }, view.invViewProjection) - view.position;
            if (LengthSquared(toCenter) < 1.0e-8f) {
                return false;
            }
            const Vector3 forward = Normalize(toCenter);

            // 視線と画面の四隅の視線のなす角のうち、いちばん大きい角の余弦
            float cosHalfAngle = 1.0f;
            constexpr float kCorners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
            for (const auto& corner : kCorners) {
                const Vector3 toCorner =
                    TransformCoord(Vector3{ corner[0], corner[1], 1.0f }, view.invViewProjection) - view.position;
                if (LengthSquared(toCorner) < 1.0e-8f) {
                    return false;
                }
                cosHalfAngle = std::min(cosHalfAngle, Dot(Normalize(toCorner), forward));
            }
            cosHalfAngle = std::clamp(cosHalfAngle, 0.05f, 1.0f);
            const float sinHalfAngle = std::sqrt(std::max(0.0f, 1.0f - cosHalfAngle * cosHalfAngle));

            // 中心は視線の上。カメラと、奥の球面の縁までの距離がそろう所に置く
            const float d = fogDistance;
            const float c = std::clamp(d / (2.0f * cosHalfAngle), d * 0.5f, d);
            const float toRim = std::sqrt(
                (d * cosHalfAngle - c) * (d * cosHalfAngle - c) + (d * sinHalfAngle) * (d * sinHalfAngle));
            radius = std::max({ c, toRim, d - c });
            center = view.position + forward * c;
            return true;
        }
    }

    bool RTVolumetricShadow::Initialize(const RTEffectInitContext& init)
    {
        Logger& log = Logger::GetInstance();
        dxCommon_ = init.dxCommon;
        asMgr_ = init.asMgr;
        if (!dxCommon_ || !asMgr_ || !init.shaderProgramCache) {
            return false;
        }

        const ShaderProgram* program = init.shaderProgramCache->GetOrCreateCompute(
            L"RTVolumetricShadow.CS.hlsl", "RTVolumetricShadow", L"cs_6_6");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTVolumetricShadow: シェーダーをコンパイルできませんでした");
            return false;
        }

        // 物の材質の表とテクスチャをヒープ内の番号で引く（Include/RayTracing/RTSceneTables.hlsli）
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());
        const auto buildResult = rootSigMgr_.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTVolumetricShadow: ルートシグネチャを作れませんでした: {}", buildResult.errorMessage);
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(program->GetReflection(),
                RTVolumetricShadowBind::kDecls, RTVolumetricShadowBind::Count, "RTVolumetricShadow");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTVolumetricShadow: シェーダーの結線が宣言と合いません: {}", e.what());
            return false;
        }

        pipelineState_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), rootSigMgr_.GetRootSignature(), program->GetCS(), "RTVolumetricShadow");
        initialized_ = pipelineState_ != nullptr;
        return initialized_;
    }

    void RTVolumetricShadow::BeginFrame()
    {
        dispatchedThisFrame_ = false;
        availableThisFrame_ = false;
        tracedCalledLastFrame_ = tracedCalledThisFrame_;
        tracedCalledThisFrame_ = false;
    }

    bool RTVolumetricShadow::CanReuseFrame(
        const Light* light, uint32_t resolution, const Vector3& center, float radius) const
    {
        const int interval = std::clamp(VolumetricFogCVars::ShadowUpdateInterval.Get(), 1, 8);
        if (!frameValid_ || !tracedCalledLastFrame_ || framesSinceTrace_ + 1 >= static_cast<uint32_t>(interval)) {
            return false;
        }
        if (light != frame_.light || resolution != resolution_) {
            return false;
        }
        if (Dot(Normalize(light->direction), frame_.direction) < kReuseMinDirectionCos) {
            return false;
        }
        const float maxShift = tracedRadius_ * kReuseMaxShiftRatio;
        return Length(center - tracedCenter_) <= maxShift && std::fabs(radius - tracedRadius_) <= maxShift;
    }

    const Light* RTVolumetricShadow::SelectLight(const LightManager& lights)
    {
        const Light* best = nullptr;
        float bestScore = 0.0f;
        const uint32_t count = lights.GetLightCount(LightType::Directional);
        for (uint32_t i = 0; i < count; ++i) {
            const Light* light = lights.GetDirectionalLight(i);
            if (!light || !light->enabled || !light->castShadows || !light->castVolumetricShadow
                || light->volumetricScatteringIntensity <= 0.0f || LengthSquared(light->direction) < 1.0e-8f) {
                continue;
            }
            const Vector3 color = lights.GetEffectiveLightColorRGB(*light);
            const float luminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
            const float score = luminance * light->intensity * light->volumetricScatteringIntensity;
            if (score > bestScore) {
                best = light;
                bestScore = score;
            }
        }
        return best;
    }

    bool RTVolumetricShadow::EnsureTexture(uint32_t resolution)
    {
        if (texture_ && resolution_ == resolution) {
            return true;
        }
        RetireTexture();

        if (!UavTexture::Create(dxCommon_->GetDevice(), *dxCommon_->GetDescriptorAllocator(), texture_,
                UavTexture::MakeDesc(resolution, resolution, 1, DXGI_FORMAT_R32_FLOAT),
                srv_, "RTVolumetricShadowDepth", uav_, "RTVolumetricShadowDepth")) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "RTVolumetricShadow: 太陽から見た深さの画像を作れませんでした");
            return false;
        }
        resolution_ = resolution;
        return true;
    }

    void RTVolumetricShadow::RetireTexture()
    {
        DeferredReleaseQueue& deferredRelease = dxCommon_->DeferredRelease();
        if (texture_) {
            deferredRelease.PushForCurrentFrame(Microsoft::WRL::ComPtr<ID3D12Resource>(texture_.Get()));
            texture_.Release();
        }
        DescriptorAllocator* allocator = dxCommon_->GetDescriptorAllocator();
        deferredRelease.PushForCurrentFrame(*allocator, srv_);
        deferredRelease.PushForCurrentFrame(*allocator, uav_);
        resolution_ = 0;
    }

    void RTVolumetricShadow::Trace(ID3D12GraphicsCommandList* cmdList,
        const ViewInfo& view,
        const LightManager& lights,
        float fogDistance,
        const RTHitTableIndices& hitTables)
    {
        // 8 の倍数にそろえる
        const uint32_t resolution =
            (static_cast<uint32_t>(std::clamp(VolumetricFogCVars::ShadowResolution.Get(), 256, 4096)) + 7u) & ~7u;
        const float casterHeight = std::max(VolumetricFogCVars::ShadowCasterHeight.Get(), 0.0f);

        // 失敗した経路でも状態がデバッグ窓に出るように、先に埋める
        dispatchInfo_ = {};
        dispatchInfo_.passName = "RTVolumetricShadow";
        dispatchInfo_.width = resolution;
        dispatchInfo_.height = resolution;
        dispatchInfo_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;

        if (!initialized_) {
            dispatchInfo_.status = RayTracingDispatchStatus::NotInitialized;
            return;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            dispatchInfo_.status = RayTracingDispatchStatus::RayTracingUnsupported;
            return;
        }
        if (!asMgr_->IsTLASReady()) {
            dispatchInfo_.status = RayTracingDispatchStatus::NoTLAS;
            return;
        }
        if (!cmdList) {
            dispatchInfo_.status = RayTracingDispatchStatus::InvalidCommandList;
            return;
        }

        const Light* light = SelectLight(lights);
        Vector3 center{};
        float radius = 0.0f;
        tracedCalledThisFrame_ = true;
        if (!light || !ComputeFogBoundingSphere(view, std::max(fogDistance, 1.0f), center, radius)) {
            dispatchInfo_.status = RayTracingDispatchStatus::Skipped;
            frameValid_ = false;
            return;
        }

        // 光源と霧の範囲がほとんど変わっていなければ、前に書いた画像と座標をそのまま使う
        if (CanReuseFrame(light, resolution, center, radius)) {
            ++framesSinceTrace_;
            availableThisFrame_ = true;
            dispatchInfo_.status = RayTracingDispatchStatus::Skipped;
            dispatchInfo_.outputSrvPtr = srv_.gpuHandle.ptr;
            dispatchInfo_.AddExtra("reused[frames]", static_cast<float>(framesSinceTrace_));
            return;
        }
        if (!EnsureTexture(resolution)) {
            dispatchInfo_.status = RayTracingDispatchStatus::OutputAllocationFailed;
            frameValid_ = false;
            return;
        }

        // 光に垂直な面の向き
        const Vector3 direction = Normalize(light->direction);
        const Vector3 reference = (std::fabs(direction.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        const Vector3 right = Normalize(Cross(reference, direction));
        const Vector3 up = Cross(direction, right);

        // カメラが動いても画素の並びがずれないよう、面の上の中心を画素の幅で丸める
        const float size = radius * 2.0f;
        const float texelSize = size / static_cast<float>(resolution);
        const float centerRight = std::floor(Dot(center, right) / texelSize + 0.5f) * texelSize;
        const float centerUp = std::floor(Dot(center, up) / texelSize + 0.5f) * texelSize;
        const Vector3 snappedCenter = right * centerRight + up * centerUp + direction * Dot(center, direction);

        // 範囲より光源の側から撃ち、範囲の外の高い物も影を落とす
        const float startDistance = radius + casterHeight;
        const Vector3 origin = snappedCenter - direction * startDistance;
        const float range = startDistance + radius;

        RTVolumetricShadowConstants constants{};
        constants.origin = origin;
        constants.size = size;
        constants.right = right;
        constants.range = range;
        constants.up = up;
        constants.resolution = resolution;
        constants.direction = direction;
        constants.texelSize = texelSize;
        // 表がそろわないフレームはアルファを調べず、どの物も不透明として当てる
        constants.hitInstanceTableIndex = hitTables.IsValid() ? hitTables.instanceTable : kRTHitNoTexture;
        constants.hitSubMeshTableIndex = hitTables.IsValid() ? hitTables.subMeshTable : kRTHitNoTexture;

        Barrier::Transition(cmdList, texture_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetComputeRootSignature(rootSigMgr_.GetRootSignature());
        cmdList->SetPipelineState(pipelineState_.Get());
        {
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(bindings_[RTVolumetricShadowBind::RTVolumetricShadowConstants],
                dxCommon_->GetUploadRing().AllocateConstants(constants));
            binder.Set(bindings_[RTVolumetricShadowBind::gScene], asMgr_->GetTLASSRVHandle());
            binder.Set(bindings_[RTVolumetricShadowBind::gSunDepth], uav_.gpuHandle);
            binder.ValidateBeforeDraw(bindings_);
        }
        cmdList->Dispatch(DivRoundUp(resolution, 8), DivRoundUp(resolution, 8), 1);
        Barrier::UAV(cmdList, texture_);
        Barrier::Transition(cmdList, texture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        frame_ = {};
        frame_.light = light;
        frame_.srv = srv_.gpuHandle;
        frame_.origin = origin;
        frame_.right = right;
        frame_.up = up;
        frame_.direction = direction;
        frame_.size = size;
        frame_.range = range;
        frame_.bias = texelSize * kBiasTexels;

        dispatchedThisFrame_ = true;
        availableThisFrame_ = true;
        frameValid_ = true;
        framesSinceTrace_ = 0;
        tracedCenter_ = center;
        tracedRadius_ = radius;
        dispatchInfo_.status = RayTracingDispatchStatus::Dispatched;
        dispatchInfo_.rayCount = static_cast<uint64_t>(resolution) * static_cast<uint64_t>(resolution);
        dispatchInfo_.outputSrvPtr = srv_.gpuHandle.ptr;
        dispatchInfo_.AddExtra("size[m]", size);
        dispatchInfo_.AddExtra("texel[cm]", texelSize * 100.0f);
        dispatchInfo_.AddExtra("rayLength[m]", range);
    }
}
