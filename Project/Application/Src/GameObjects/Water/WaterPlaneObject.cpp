#include "pch.h"
#include "WaterPlaneObject.h"
#include "Scene/IScene.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "WaterShaderResourceBinder.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include <cmath>
#include <filesystem>

WaterPlaneObject::WaterPlaneObject(float size, uint32_t resolution, bool useFFTOcean)
    : size_(size)
    , resolution_(resolution)
    , useFFTOcean_(useFFTOcean)
    , scrollSpeed_({ 0.03f, 0.01f })
    , uvTiling_({ 4.0f, 4.0f })
    , uvOffset_({ 0.0f, 0.0f }) {
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

void WaterPlaneObject::DrawShadow(ID3D12GraphicsCommandList* cmdList) {
    (void)cmdList;
}

CoreEngine::RenderItem WaterPlaneObject::BuildRenderItem() const {
    CoreEngine::RenderItem item = CoreEngine::PrimitiveGameObject::BuildRenderItem();
    item.kind = CoreEngine::RenderItemKind::WaterSurface;
    item.passType = CoreEngine::RenderPassType::WaterSurface;
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
    SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);

    // 定数バッファを作成する
    auto* engine = GetEngineSystem();
    auto* dxCommon = engine ? engine->GetComponent<CoreEngine::DirectXCommon>() : nullptr;
    if (dxCommon) {
        constantBuffers_.Initialize(dxCommon->GetDevice());
        constantBuffers_.UpdateWaterConstants(waterCB_);
        constantBuffers_.UpdateFrameConstants(frameCB_, false);
        constantBuffers_.UpdateFrameConstants(frameCB_, true);
    }
}

void WaterPlaneObject::RebuildWaterShaderPipeline() {
    auto* engine = GetEngineSystem();
    auto* dxCommon = engine ? engine->GetComponent<CoreEngine::DirectXCommon>() : nullptr;
    auto* modelManager = engine ? engine->GetComponent<CoreEngine::ModelManager>() : nullptr;
    if (!dxCommon || !modelManager) {
        return;
    }

    // 旧カスタムPSOがGPU実行中のまま破棄されると
    // OBJECT_DELETED_WHILE_STILL_IN_USE が発生するため、切替時のみ同期する。
    CoreEngine::Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::Pipeline,
        "WaterPlane: begin shader pipeline rebuild. wait for GPU before PSO replacement. useFFTOcean={}",
        useFFTOcean_);
    dxCommon->WaitForPreviousFrame();

    SetCustomShaderProvider(this);
    BuildCustomShaderPipelineIfNeeded(dxCommon->GetDevice(), modelManager);

    CoreEngine::Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::Pipeline,
        "WaterPlane: end shader pipeline rebuild. useFFTOcean={}",
        useFFTOcean_);
}

void WaterPlaneObject::BindCustomResources(
    ID3D12GraphicsCommandList* cmdList,
    const CoreEngine::CustomShaderPipeline* pipeline) const {

    const D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress = constantBuffers_.GetWaterCBGpuAddress();
    if (!cmdList || !pipeline || waterCBGpuAddress == 0) {
        return;
    }

    if (frameCB_.depthFadeDebugEnabled != 0) {
        const D3D12_GPU_VIRTUAL_ADDRESS selectedFrameCBGpuAddress =
            constantBuffers_.GetFrameCBGpuAddress(frameCB_.clipEnabled != 0);
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::Pipeline,
            "WaterPlane BindCustomResources: b4={} b5={} reflSRV=0x{:X} depthSRV=0x{:X} sceneColorSRV=0x{:X} refractionColorSRV=0x{:X} clipEnabled={} reflectionEnabled={} depthFadeEnabled={} debugMode={}",
            waterCBGpuAddress,
            selectedFrameCBGpuAddress,
            renderResources_.reflectionSRV.ptr,
            renderResources_.sceneDepthSRV.ptr,
            renderResources_.sceneColorSRV.ptr,
            renderResources_.refractionColorSRV.ptr,
            frameCB_.clipEnabled,
            frameCB_.reflectionEnabled,
            frameCB_.depthFadeEnabled,
            frameCB_.depthDebugViewMode);
    }

    const D3D12_GPU_VIRTUAL_ADDRESS selectedFrameCBGpuAddress =
        constantBuffers_.GetFrameCBGpuAddress(frameCB_.clipEnabled != 0);
    // Water 専用のバインダへ委譲して CBV / SRV の接続を行う
    WaterShaderResourceBinder::Bind(
        cmdList,
        pipeline,
        waterCBGpuAddress,
        selectedFrameCBGpuAddress,
        renderResources_);

    if (renderResources_.fftDisplacementSRV.ptr != 0) {
        static uint32_t sFftBindLogCounter = 0;
        if ((sFftBindLogCounter++ % 240) == 0) {
            const int fftDisplacementSlot = pipeline->GetRootParamIndex("gFFTOceanDisplacement");
            CoreEngine::Logger::GetInstance().Infof(
                CoreEngine::LogCategory::Graphics,
                CoreEngine::LogSubCategory::Pipeline,
                "WaterPlane: FFT displacement bound. slot={} srv=0x{:X} useFFTOcean={} hasFFTSRVs={}",
                fftDisplacementSlot,
                renderResources_.fftDisplacementSRV.ptr,
                useFFTOcean_,
                HasFFTOceanTextureSRVs());
        }
    }
}

void WaterPlaneObject::SetUseFFTOcean(bool useFFTOcean) {
    if (useFFTOcean_ == useFFTOcean) {
        return;
    }

    useFFTOcean_ = useFFTOcean;
    frameCB_.useFFTOceanNormalMap = useFFTOcean_ ? 1 : 0;
    RebuildWaterShaderPipeline();
    UpdateFrameConstants();

    CoreEngine::Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::Pipeline,
        "WaterPlane: switched water shader path. useFFTOcean={} hasFFTSRVs={} vertexShader={} pixelShader={}",
        useFFTOcean_,
        HasFFTOceanTextureSRVs(),
        std::filesystem::path(GetVertexShaderPath()).string(),
        std::filesystem::path(GetPixelShaderPath()).string());
}

void WaterPlaneObject::SetScrollSpeed(const CoreEngine::Vector2& speed) {
    scrollSpeed_ = speed;
}

void WaterPlaneObject::SetUVTiling(const CoreEngine::Vector2& tiling) {
    uvTiling_ = tiling;
}

void WaterPlaneObject::SetWave(uint32_t index, const WaveParams& wave) {
    if (index < kMaxWaterWaveCount) {
        waterCB_.waves[index] = wave;
    }
}

void WaterPlaneObject::SetFFTOceanTextureSRVs(
    D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle) {
    renderResources_.SetFFTOceanTextureSRVs(
        displacementSrvHandle,
        normalSrvHandle,
        jacobianSrvHandle);

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetFFTOceanTextureSRVs: displacementSRV=0x{:X} normalSRV=0x{:X} jacobianSRV=0x{:X} useFFTOcean={}",
            renderResources_.fftDisplacementSRV.ptr,
            renderResources_.fftNormalSRV.ptr,
            renderResources_.fftJacobianSRV.ptr,
            useFFTOcean_);
    }
}

void WaterPlaneObject::SetAtmosphereAPResources(
    D3D12_GPU_VIRTUAL_ADDRESS atmosphereCB,
    D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE skyViewSrvHandle,
    bool enabled) {
    renderResources_.atmosphereCB = atmosphereCB;
    renderResources_.cameraVolumeSRV = cameraVolumeSrvHandle;
    renderResources_.skyViewSRV = skyViewSrvHandle;
    // リソースが揃っていない場合はシェーダー側の参照を止める
    frameCB_.aerialPerspectiveEnabled = (enabled && renderResources_.HasAtmosphere()) ? 1 : 0;
}

void WaterPlaneObject::SetRefractionColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    renderResources_.refractionColorSRV = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetRefractionColorSRV: srv=0x{:X}",
            renderResources_.refractionColorSRV.ptr);
    }
}

void WaterPlaneObject::SetActiveWaveCount(uint32_t count) {
    waterCB_.activeWaveCount = (count > kMaxWaterWaveCount) ? kMaxWaterWaveCount : count;
}

void WaterPlaneObject::SetReflectionTexture(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    renderResources_.reflectionSRV = srvHandle;
    // ハンドルが有効なときだけ反射テクスチャを有効にする
    frameCB_.reflectionEnabled = (srvHandle.ptr != 0) ? 1 : 0;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetReflectionTexture: srv=0x{:X} reflectionEnabled={}",
            renderResources_.reflectionSRV.ptr,
            frameCB_.reflectionEnabled);
    }
}

void WaterPlaneObject::SetClipPlane(const CoreEngine::Vector4& clipPlane, bool enable) {
    frameCB_.clipPlane[0] = clipPlane.x;
    frameCB_.clipPlane[1] = clipPlane.y;
    frameCB_.clipPlane[2] = clipPlane.z;
    frameCB_.clipPlane[3] = clipPlane.w;
    frameCB_.clipEnabled  = enable ? 1 : 0;
}

void WaterPlaneObject::UpdateFrameConstants() {
    constantBuffers_.UpdateFrameConstants(frameCB_, frameCB_.clipEnabled != 0);

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::Pipeline,
            "WaterPlane UpdateFrameConstants: clipEnabled={} reflectionEnabled={} depthFadeEnabled={} debugMode={} absorptionCoeff={:.3f} fresnelScale={:.3f} fresnelF0={:.4f} shallow=({:.3f}, {:.3f}, {:.3f}) deep=({:.3f}, {:.3f}, {:.3f})",
            frameCB_.clipEnabled,
            frameCB_.reflectionEnabled,
            frameCB_.depthFadeEnabled,
            frameCB_.depthDebugViewMode,
            frameCB_.absorptionCoeff,
            frameCB_.fresnelReflectanceScale,
            frameCB_.fresnelBaseReflectance,
            frameCB_.shallowColor[0],
            frameCB_.shallowColor[1],
            frameCB_.shallowColor[2],
            frameCB_.deepColor[0],
            frameCB_.deepColor[1],
            frameCB_.deepColor[2]);
    }
}

void WaterPlaneObject::SetFresnelParameters(float reflectanceScale, float baseReflectance) {
    frameCB_.fresnelReflectanceScale = reflectanceScale;
    frameCB_.fresnelBaseReflectance = baseReflectance;
}

void WaterPlaneObject::SetSceneDepthSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    renderResources_.sceneDepthSRV = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetSceneDepthSRV: srv=0x{:X}",
            renderResources_.sceneDepthSRV.ptr);
    }
}

void WaterPlaneObject::SetSceneColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    renderResources_.sceneColorSRV = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetSceneColorSRV: srv=0x{:X}",
            renderResources_.sceneColorSRV.ptr);
    }
}

void WaterPlaneObject::ApplyWaterReflectionResult(const CoreEngine::RenderViewResult& result)
{
    // ReflectionView 出力は反射テクスチャとしてのみ使用する。
    // SceneDepth / SceneColor は GameView 側の SRV を別経路で設定する。
    SetReflectionTexture(result.viewSrv);

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane ApplyWaterReflectionResult: reflectionSRV=0x{:X} sceneDepthSRV(ignored)=0x{:X} sceneColorSRV(ignored)=0x{:X}",
            result.viewSrv.ptr,
            result.sceneDepthSrv.ptr,
            result.sceneColorSrv.ptr);
    }
}

void WaterPlaneObject::SetDepthFade(float absorptionCoeff, bool enabled) {
    frameCB_.absorptionCoeff  = absorptionCoeff;
    frameCB_.depthFadeEnabled = enabled ? 1 : 0;
}

void WaterPlaneObject::SetDepthFadeDebug(bool enabled, float debugScale) {
    frameCB_.depthFadeDebugEnabled = enabled ? 1 : 0;
    frameCB_.depthFadeDebugScale = debugScale;
    CoreEngine::Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::Pipeline,
        "WaterPlane depth fade debug changed: enabled={} debugScale={:.3f}",
        frameCB_.depthFadeDebugEnabled,
        frameCB_.depthFadeDebugScale);
}

void WaterPlaneObject::SetDepthDebugViewMode(WaterDebugViewMode mode) {
    frameCB_.depthDebugViewMode = static_cast<uint32_t>(mode);
    CoreEngine::Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::Pipeline,
        "WaterPlane debug view mode changed: mode={} debugEnabled={} debugScale={:.3f}",
        frameCB_.depthDebugViewMode,
        frameCB_.depthFadeDebugEnabled,
        frameCB_.depthFadeDebugScale);
}

void WaterPlaneObject::SetWaterColors(const CoreEngine::Vector3& shallowColor, const CoreEngine::Vector3& deepColor) {
    frameCB_.shallowColor[0] = shallowColor.x;
    frameCB_.shallowColor[1] = shallowColor.y;
    frameCB_.shallowColor[2] = shallowColor.z;
    frameCB_.deepColor[0]    = deepColor.x;
    frameCB_.deepColor[1]    = deepColor.y;
    frameCB_.deepColor[2]    = deepColor.z;
}

void WaterPlaneObject::SetBaseColor(const CoreEngine::Vector4& color) {
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

void WaterPlaneObject::UpdateUVScroll(float deltaTime) {
    // 旧呼び出し経路との互換のため、UV 更新と simulation 時間更新をまとめて行う
    UpdateUVAnimation(deltaTime);

    // 経過時間を加算（波の位相計算に使用）
    elapsedTime_ += deltaTime;
    SetSimulationTime(elapsedTime_);
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

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> WaterPlaneObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::PlaneMeshGenerator>(
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
    CoreEngine::Matrix4x4 uvMat = CoreEngine::MathCore::Matrix::Identity();
    uvMat.m[0][0] = uvTiling_.x;
    uvMat.m[1][1] = uvTiling_.y;
    uvMat.m[3][0] = uvOffset_.x;
    uvMat.m[3][1] = uvOffset_.y;

    mat->SetUVTransform(uvMat);
}
