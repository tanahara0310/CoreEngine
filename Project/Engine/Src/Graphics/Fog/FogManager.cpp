#include "pch.h"
#include "Graphics/Fog/FogManager.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Fog/Settings/FogCVars.h"
#include "Graphics/Fog/Shader/FogBindings.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <cstring>
#include <exception>

namespace CoreEngine
{
    namespace
    {
        /// @brief 合成 CS のシェーダー供給（パスはファイル名で AssetDatabase が解決する）
        class HeightFogShaderProvider final : public ICustomShaderProvider {
        public:
            std::wstring GetComputeShaderPath() const override { return L"HeightFog.CS.hlsl"; }
        };

        constexpr uint32_t kThreadGroupSize = 8;
    }

    bool FogManager::Initialize(GraphicsCore* graphicsCore)
    {
        graphicsCore_ = graphicsCore;
        if (!graphicsCore_) {
            return false;
        }

        ID3D12Device* device = graphicsCore_->GetDevice();
        if (!device) {
            return false;
        }

        // DXC は構築 1 回につき 1 個で足りる
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const HeightFogShaderProvider provider;
        if (!pipeline_.Build(device, shaderCompiler, reflectionBuilder, provider)
            || !pipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "FogManager: HeightFog コンピュートパイプラインの構築に失敗");
            return false;
        }

        const ShaderReflectionData* reflection = pipeline_.GetComputeReflection();
        if (!reflection) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "FogManager: HeightFog のリフレクションを取得できません");
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(*reflection, FogApplyBind::kDecls, "HeightFog");
        }
        catch (const std::exception&) {
            // 違反の内訳は BindingTable::Resolve が error ログへ出している
            return false;
        }

        // 常設の「何もしない」フォグ定数を 1 本作る。PrepareConstants が走らないビュー・
        // フレームでも、前方描画が必ず有効な CBV を差せるようにするための保険
        {
            FogConstants disabled{};
            disabled.invViewProj = MathCore::Matrix::Identity();
            disabled.sunDirection = Vector3{ 0.0f, -1.0f, 0.0f };
            disabled.sunTint = Vector3{ 1.0f, 1.0f, 1.0f };
            disabled.sunGain = 1.0f;
            disabled.sunExponent = 1.0f;

            constexpr UINT size = (sizeof(FogConstants) + 255) & ~255u;
            disabledConstantBuffer_ = ResourceFactory::CreateBufferResource(device, size);
            if (!disabledConstantBuffer_) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "FogManager: 恒等フォグ定数バッファの確保に失敗");
                return false;
            }
            void* mapped = nullptr;
            if (FAILED(disabledConstantBuffer_->Map(0, nullptr, &mapped)) || !mapped) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "FogManager: 恒等フォグ定数バッファの Map に失敗");
                return false;
            }
            std::memcpy(mapped, &disabled, sizeof(disabled));
            disabledConstantBuffer_->Unmap(0, nullptr);
            disabledConstantsAddress_ = disabledConstantBuffer_->GetGPUVirtualAddress();
        }

        pipelineReady_ = true;
        return true;
    }

    void FogManager::Update(const Vector3& sunDirection, bool hasSun)
    {
        // Update() を呼ぶのはフォグを使うシーンのみ。このフレームは合成を有効にする
        fogActive_ = true;

        // 設定は CVar が保持する。ダーティ判定を持つ資源が無いので毎フレーム取り込んでよい
        FogCVars::LoadInto(settings_);

        // 内散乱は正規化済みの向きを前提にする（呼び出し側の正規化漏れをここで吸収する）。
        // 太陽が無いフレームは向きを維持したまま hasSun_ だけ倒す（強度側で切る）
        const float lengthSq = LengthSquared(sunDirection);
        const bool directionValid = (lengthSq > 1.0e-8f);
        if (directionValid) {
            sunDirection_ = Normalize(sunDirection);
        }
        hasSun_ = hasSun && directionValid;
    }

    FogManager::FogConstants FogManager::BuildConstants(const ViewInfo& view, const FogSkyInfo& sky) const
    {
        FogConstants constants{};
        constants.invViewProj = view.invViewProjection;
        constants.cameraWorldPos = view.position;
        // フォグが無効なフレームは密度 0 で恒等にする。全画面合成は走らないが、
        // 前方描画は毎フレーム同じ CBV を差すので、ここで無効化しておく必要がある
        constants.density = IsFogActive() ? settings_.density : 0.0f;
        constants.fogColor = settings_.color * settings_.colorIntensity;
        constants.heightFalloff = settings_.heightFalloff;
        constants.heightRef = settings_.heightRefM;
        constants.startDistance = settings_.startDistanceM;
        constants.maxOpacity = settings_.maxOpacity;
        // 0 以下は「ファークリップまで」の意味にする（設定を触らずに済ませるための既定動作）
        constants.skyDistance = (settings_.skyDistanceM > 0.0f)
            ? settings_.skyDistanceM : view.farZ;
        constants.applyToSky = settings_.applyToSky ? 1u : 0u;

        constants.sunDirection = sunDirection_;
        constants.sunExponent = settings_.sunExponent;
        // 太陽ライトが無いフレームは内散乱を恒等にする。
        // 残すと「存在しない太陽の方向」だけフォグが明るくなる
        constants.sunTint = hasSun_ ? settings_.sunTint : Vector3{ 1.0f, 1.0f, 1.0f };
        constants.sunGain = hasSun_ ? settings_.sunGain : 1.0f;

        // 大気が無い（Sky-View LUT が未生成の）フレームはブレンドを切る。
        // シェーダーはこの値が 0 のとき LUT をサンプルしないので、未バインドでも安全
        constants.skyColorBlend = sky.valid ? settings_.skyColorBlend : 0.0f;
        constants.cameraRadiusKm = sky.cameraRadiusKm;
        constants.planetRadiusKm = sky.planetRadiusKm;

        return constants;
    }

    void FogManager::PrepareConstants(const ViewInfo& view, const FogSkyInfo& sky)
    {
        if (!graphicsCore_ || !view.isValid) {
            return;
        }

        // 定数は毎フレーム UploadRing から取る（専用バッファを 1 本持って上書きすると、
        // GPU が前フレームのディスパッチを実行する前に CPU が書き潰す）
        UploadRing& ring = graphicsCore_->GetUploadRing();

        const FogConstants full = BuildConstants(view, sky);
        frameConstants_[static_cast<size_t>(FogVariant::Full)] = ring.AllocateConstants(full);

        // 加算・スクリーン合成用: 内散乱色を 0 にすると ApplyFog の lerp が
        // lerp(0, color, T) = color * T になり、減衰だけが残る。
        // 内散乱を足すと、背後の不透明面に既に乗っている分と二重になって光る
        FogConstants additive = full;
        additive.fogColor = Vector3{ 0.0f, 0.0f, 0.0f };
        additive.skyColorBlend = 0.0f;
        additive.sunTint = Vector3{ 1.0f, 1.0f, 1.0f };
        additive.sunGain = 1.0f;
        frameConstants_[static_cast<size_t>(FogVariant::Additive)] = ring.AllocateConstants(additive);

        // 不透明フォワード用: 全画面パスが深度から掛けるので、描画側では何もしない
        FogConstants disabled = full;
        disabled.density = 0.0f;
        frameConstants_[static_cast<size_t>(FogVariant::Disabled)] = ring.AllocateConstants(disabled);
    }

    void FogManager::ApplyFog(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& sceneColor,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUav,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        const ViewInfo& view,
        const FogSkyInfo& sky)
    {
        if (!cmdList || !graphicsCore_ || !pipelineReady_ || !view.isValid) {
            return;
        }

        // 定数は FogPass が ApplyFog の前に PrepareConstants で確保済み
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = FogApplyBind;
            cmdList->SetPipelineState(pipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(pipeline_.GetComputeRootSignature());

            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(bindings_[B::gFog], GetConstantsGpuAddress(FogVariant::Full));
            binder.Set(bindings_[B::gSceneDepth], sceneDepthSrv);
            binder.Set(bindings_[B::gOutput], sceneColorUav);
            // Sky-View LUT はフレームごとの判断で差す（Conditional 宣言）。
            // 差さないフレームは skyColorBlend が 0 なのでシェーダーが触れない
            if (sky.valid && sky.skyViewLutSrv.ptr != 0) {
                binder.Set(bindings_[B::gSkyViewLUT], sky.skyViewLutSrv);
            }
            binder.ValidateBeforeDraw(bindings_);
        }

        const D3D12_RESOURCE_DESC desc = sceneColor.Desc();
        cmdList->Dispatch(
            (static_cast<UINT>(desc.Width) + kThreadGroupSize - 1) / kThreadGroupSize,
            (desc.Height + kThreadGroupSize - 1) / kThreadGroupSize,
            1);

        // 書き込み完了を待たせてから、後続パス（Transparent 等）の想定状態へ戻す
        Barrier::UAV(cmdList, sceneColor);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
