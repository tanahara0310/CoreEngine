#include "pch.h"
#include "Graphics/Water/WaterCVars.h"
#include "WaterPlaneObject.h"

#include "GameObject/Component/Scene/SceneTagComponent.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Water/Surface/WaterShaderResourceBinder.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include <cmath>
#include <filesystem>

namespace CoreEngine
{
    bool WaterPlaneObject::WritesMotionVector() const
    {
        return WaterCVars::WriteMotionVector.Get();
    }

    WaterPlaneObject::WaterPlaneObject(float size, uint32_t resolution, bool useFFTOcean)
        : size_(size)
        , resolution_(resolution)
        , useFFTOcean_(useFFTOcean)
        , scrollSpeed_({ 0.03f, 0.01f })
        , uvTiling_({ 4.0f, 4.0f })
        , uvOffset_({ 0.0f, 0.0f }) {
        // 「シーンの水面」タグ（WaterRenderFeature が具象型を知らずに見つけるため）
        AddComponent<SceneTagComponent<WaterPlaneObject>>(this);

        waterCB_.activeWaveCount = kMaxWaterWaveCount;
        frameCB_.useFFTOceanNormalMap = useFFTOcean_ ? 1 : 0;

        // デフォルトの波パラメータを設定（16 本の Gerstner Wave を重ね合わせる）
        waterCB_.waves[0]  = { {  1.0f,  0.0f }, 0.18f, 14.0f, 1.6f, 0.24f, 0.0f };
        waterCB_.waves[1]  = { {  0.7f,  0.7f }, 0.10f, 10.0f, 2.0f, 0.16f, 1.4f };
        waterCB_.waves[2]  = { { -0.5f,  0.8f }, 0.06f,  7.0f, 2.3f, 0.10f, 2.7f };
        waterCB_.waves[3]  = { {  0.2f, -0.9f }, 0.03f,  5.0f, 2.7f, 0.06f, 4.1f };
        waterCB_.waves[4]  = { {  0.9f,  0.3f }, 0.028f, 16.0f, 1.3f, 0.05f, 0.7f };
        waterCB_.waves[5]  = { {  0.3f,  0.9f }, 0.024f, 12.0f, 1.5f, 0.05f, 1.6f };
        waterCB_.waves[6]  = { { -0.8f,  0.2f }, 0.021f, 10.0f, 1.8f, 0.04f, 2.5f };
        waterCB_.waves[7]  = { { -0.6f, -0.5f }, 0.018f,  8.5f, 2.0f, 0.04f, 3.4f };
        waterCB_.waves[8]  = { {  0.4f, -0.9f }, 0.016f,  7.5f, 2.2f, 0.03f, 4.2f };
        waterCB_.waves[9]  = { {  1.0f, -0.1f }, 0.014f, 15.0f, 1.2f, 0.03f, 0.9f };
        waterCB_.waves[10] = { {  0.1f,  1.0f }, 0.013f, 13.0f, 1.4f, 0.03f, 1.9f };
        waterCB_.waves[11] = { { -0.9f,  0.4f }, 0.012f, 11.0f, 1.7f, 0.02f, 2.8f };
        waterCB_.waves[12] = { { -0.2f, -1.0f }, 0.011f,  9.0f, 2.0f, 0.02f, 3.7f };
        waterCB_.waves[13] = { {  0.8f, -0.6f }, 0.010f,  7.0f, 2.2f, 0.02f, 4.6f };
        waterCB_.waves[14] = { {  0.6f,  0.8f }, 0.009f,  6.0f, 2.4f, 0.02f, 5.3f };
        waterCB_.waves[15] = { { -0.7f,  0.6f }, 0.008f,  5.5f, 2.6f, 0.01f, 6.0f };
    }

    RenderItem WaterPlaneObject::BuildRenderItem() const {
        RenderItem item = PrimitiveGameObject::BuildRenderItem();
        item.kind = RenderItemKind::WaterSurface;
        item.passType = RenderPassType::WaterSurface;
        return item;
    }

    std::wstring WaterPlaneObject::GetVertexShaderPath() const {
        return useFFTOcean_
            ? L"FFTWater.VS.hlsl"
            : L"Water.VS.hlsl";
    }

    std::wstring WaterPlaneObject::GetPixelShaderPath() const {
        return L"Water.PS.hlsl";
    }

    void WaterPlaneObject::OnInitialize() {
        // 独自シェーダーを使用するよう登録する
        SetCustomShaderProvider(this);

        // 水面は半透明オブジェクトとして描画する
        SetBlendMode(BlendMode::kBlendModeNormal);

        // 定数バッファを作成する
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine ? engine->GetService<DirectXCommon>() : nullptr;
        if (dxCommon) {
            constantBuffers_.Initialize(dxCommon->GetDevice());
            constantBuffers_.UpdateWaterConstants(waterCB_);
            constantBuffers_.UpdateFrameConstants(frameCB_);
        }
    }

    void WaterPlaneObject::RebuildWaterShaderPipeline() {
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine ? engine->GetService<DirectXCommon>() : nullptr;
        auto* modelManager = engine ? engine->GetService<ModelManager>() : nullptr;
        if (!dxCommon || !modelManager) {
            return;
        }

        // 旧カスタムPSOがGPU実行中のまま破棄されると
        // OBJECT_DELETED_WHILE_STILL_IN_USE が発生するため、切替時のみ同期する。
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterPlane: begin shader pipeline rebuild. wait for GPU before PSO replacement. useFFTOcean={}",
            useFFTOcean_);
        dxCommon->WaitForPreviousFrame();

        SetCustomShaderProvider(this);
        BuildCustomShaderPipelineIfNeeded(dxCommon->GetDevice(), modelManager);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterPlane: end shader pipeline rebuild. useFFTOcean={}",
            useFFTOcean_);
    }

    void WaterPlaneObject::BindCustomResources(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline* pipeline) const {

        const D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress = constantBuffers_.GetWaterCBGpuAddress();
        if (!cmdList || !pipeline || waterCBGpuAddress == 0) {
            return;
        }

        // Water 専用のバインダへ委譲して CBV / SRV の接続を行う
        WaterShaderResourceBinder::Bind(
            cmdList,
            pipeline,
            waterCBGpuAddress,
            constantBuffers_.GetFrameCBGpuAddress(),
            renderResources_);
    }

    void WaterPlaneObject::ApplyFrameBinding(const WaterFrameBinding& binding) {
        renderResources_ = binding.resources;

        // ---- SRV / リソースの実在から、シェーダー側の参照フラグを 1 箇所で導出する ----
        // 以前は 5 個の setter がそれぞれ「自分の担当フラグ」を立てており、
        // 「SRV は繋がっているのにフラグが前フレームのまま」という食い違いが起きうる形だった。
        frameCB_.reflectionEnabled = renderResources_.HasReflectionTexture() ? 1 : 0;
        frameCB_.aerialPerspectiveEnabled =
            (binding.aerialPerspectiveEnabled && renderResources_.HasAtmosphere()) ? 1 : 0;
        frameCB_.skyAmbientEnabled =
            (binding.skyAmbientEnabled && renderResources_.skyIrradianceSRV.ptr != 0) ? 1 : 0;
        frameCB_.skyEnvReflectionEnabled =
            (binding.skyEnvReflectionEnabled && renderResources_.skyEnvironmentSRV.ptr != 0) ? 1 : 0;
        frameCB_.skyAmbientScale = binding.skyAmbientScale;

        // 不正なクリップ距離（0 や逆転）はシェーダーの LinearizeDepth を破綻させるため、
        // 直前の有効値を維持する
        if (binding.cameraNearZ > 0.0f && binding.cameraFarZ > binding.cameraNearZ) {
            frameCB_.cameraNearZ = binding.cameraNearZ;
            frameCB_.cameraFarZ = binding.cameraFarZ;
        }

        UploadFrameConstants();
    }

    void WaterPlaneObject::UploadFrameConstants() {
        constantBuffers_.UpdateFrameConstants(frameCB_);
    }

    void WaterPlaneObject::SetUseFFTOcean(bool useFFTOcean) {
        if (useFFTOcean_ == useFFTOcean) {
            return;
        }

        useFFTOcean_ = useFFTOcean;
        frameCB_.useFFTOceanNormalMap = useFFTOcean_ ? 1 : 0;
        RebuildWaterShaderPipeline();
        UploadFrameConstants();

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterPlane: switched water shader path. useFFTOcean={} hasFFTSRVs={} vertexShader={} pixelShader={}",
            useFFTOcean_,
            HasFFTOceanTextureSRVs(),
            std::filesystem::path(GetVertexShaderPath()).string(),
            std::filesystem::path(GetPixelShaderPath()).string());
    }

    void WaterPlaneObject::SetScrollSpeed(const Vector2& speed) {
        scrollSpeed_ = speed;
    }

    void WaterPlaneObject::SetUVTiling(const Vector2& tiling) {
        uvTiling_ = tiling;
    }

    void WaterPlaneObject::SetWave(uint32_t index, const WaveParams& wave) {
        if (index < kMaxWaterWaveCount) {
            waterCB_.waves[index] = wave;
        }
    }

    void WaterPlaneObject::SetActiveWaveCount(uint32_t count) {
        waterCB_.activeWaveCount = (count > kMaxWaterWaveCount) ? kMaxWaterWaveCount : count;
    }

    void WaterPlaneObject::SetFresnelParameters(float reflectanceScale, float baseReflectance) {
        frameCB_.fresnelReflectanceScale = reflectanceScale;
        frameCB_.fresnelBaseReflectance = baseReflectance;
    }

    void WaterPlaneObject::SetDepthFade(bool enabled) {
        frameCB_.depthFadeEnabled = enabled ? 1 : 0;
    }

    void WaterPlaneObject::SetDepthFadeDebug(bool enabled, float debugScale) {
        frameCB_.depthFadeDebugEnabled = enabled ? 1 : 0;
        frameCB_.depthFadeDebugScale = debugScale;
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterPlane depth fade debug changed: enabled={} debugScale={:.3f}",
            frameCB_.depthFadeDebugEnabled,
            frameCB_.depthFadeDebugScale);
    }

    void WaterPlaneObject::SetDepthDebugViewMode(WaterDebugViewMode mode) {
        frameCB_.depthDebugViewMode = static_cast<uint32_t>(mode);
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterPlane debug view mode changed: mode={} debugEnabled={} debugScale={:.3f}",
            frameCB_.depthDebugViewMode,
            frameCB_.depthFadeDebugEnabled,
            frameCB_.depthFadeDebugScale);
    }

    float WaterPlaneObject::ComputeFoamWindCoverageScale(float windSpeed) {
        // Monahan の白波被覆率 W = 3.84e-6 · U^3.41。基準風速との比を取ると係数が消える。
        // 基準風速 = FoamBias を較正した風速。ここを変えるなら Bias も較正し直すこと。
        constexpr float kReferenceWindSpeed = 18.0f;
        constexpr float kMonahanExponent = 3.41f;
        const float ratio = (std::max)(windSpeed, 0.0f) / kReferenceWindSpeed;
        return (std::min)(std::pow(ratio, kMonahanExponent), 1.0f);
    }

    void WaterPlaneObject::SetFoamParameters(
        bool enabled, float bias, float gain, float opacity,
        const Vector3& cascadeWeights, float decaySeconds,
        float windCoverageScale) {
        frameCB_.foamEnabled = enabled ? 1 : 0;
        frameCB_.foamBias = bias;
        frameCB_.foamGain = gain;
        frameCB_.foamOpacity = opacity;
        frameCB_.foamCascadeWeights[0] = cascadeWeights.x;
        frameCB_.foamCascadeWeights[1] = cascadeWeights.y;
        frameCB_.foamCascadeWeights[2] = cascadeWeights.z;
        frameCB_.foamDecaySeconds = decaySeconds;
        frameCB_.foamWindCoverageScale = windCoverageScale;
    }

    void WaterPlaneObject::SetWaterOpticalCoefficients(const Vector3& absorptionCoeff, const Vector3& scatteringCoeff) {
        frameCB_.absorptionCoeff[0] = absorptionCoeff.x;
        frameCB_.absorptionCoeff[1] = absorptionCoeff.y;
        frameCB_.absorptionCoeff[2] = absorptionCoeff.z;
        frameCB_.scatteringCoeff[0] = scatteringCoeff.x;
        frameCB_.scatteringCoeff[1] = scatteringCoeff.y;
        frameCB_.scatteringCoeff[2] = scatteringCoeff.z;
    }

    void WaterPlaneObject::SetBaseColor(const Vector4& color) {
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (mat) { mat->SetColor(color); }
    }

    void WaterPlaneObject::SetRoughness(float roughness) {
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (mat) { mat->SetRoughness(roughness); }
    }

    void WaterPlaneObject::SetMetallic(float metallic) {
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (mat) { mat->SetMetallic(metallic); }
    }

    void WaterPlaneObject::SetIBLEnabled(bool enable) {
        // IBL の有効/無効はシーン側で決まるため、マテリアル側は強度によるオプトアウトで表現する
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (mat) { mat->SetIBLIntensity(enable ? 1.0f : 0.0f); }
    }

    void WaterPlaneObject::UpdateUVAnimation(float deltaTime) {
        // 経過時間を加算（波の位相計算に使用）
        // UV オフセットを速度 × 時間で加算
        uvOffset_.x += scrollSpeed_.x * deltaTime;
        uvOffset_.y += scrollSpeed_.y * deltaTime;

        // 0〜1 の範囲内に折り返す（精度劣化防止）
        uvOffset_.x = std::fmod(uvOffset_.x, 1.0f);
        uvOffset_.y = std::fmod(uvOffset_.y, 1.0f);

        ApplyUVTransform();
    }

    void WaterPlaneObject::SetSimulationTime(float timeSeconds) {
        // simulation 層が決定した時間を CPU / GPU の WaterConstants へ反映する
        elapsedTime_ = timeSeconds;
        waterCB_.time = timeSeconds;
        constantBuffers_.UpdateWaterConstants(waterCB_);
    }

    std::unique_ptr<IPrimitiveMeshGenerator> WaterPlaneObject::CreateMeshGenerator() const {
        return std::make_unique<PlaneMeshGenerator>(
            size_, size_, resolution_, resolution_);
    }

    void WaterPlaneObject::ApplyUVTransform() {
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (!mat) {
            return;
        }

        // UV 変換行列: Scale（タイリング） × Translate（スクロールオフセット）
        // HLSL 側は float4(uv, 0, 1) × uvTransform の行ベクトル乗算なので
        // 行列の m[0][0], m[1][1] がスケール、m[3][0], m[3][1] がオフセット
        Matrix4x4 uvMat = MathCore::Matrix::Identity();
        uvMat.m[0][0] = uvTiling_.x;
        uvMat.m[1][1] = uvTiling_.y;
        uvMat.m[3][0] = uvOffset_.x;
        uvMat.m[3][1] = uvOffset_.y;

        mat->SetUVTransform(uvMat);
    }
}
