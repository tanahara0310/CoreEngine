#include "pch.h"
#include "WaterPlaneObject.h"
#include "Scene/IScene.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include <cmath>
#include <cstring>
#include <cassert>

// ---- 定数バッファのサイズは 256 バイトアライメント ----
static constexpr UINT kWaterCBSize =
(sizeof(WaterConstants) + 255) & ~255u;

static constexpr UINT kFrameCBSize =
(sizeof(WaterFrameConstants) + 255) & ~255u;

WaterPlaneObject::WaterPlaneObject(float size, uint32_t resolution)
    : size_(size)
    , resolution_(resolution)
    , scrollSpeed_({ 0.03f, 0.01f })
    , uvTiling_({ 4.0f, 4.0f })
    , uvOffset_({ 0.0f, 0.0f }) {
    waterCB_.activeWaveCount = kMaxWaterWaveCount;

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

void WaterPlaneObject::OnInitialize() {
    // 独自シェーダーを使用するよう登録する
    SetCustomShaderProvider(this);

    // 水面は半透明オブジェクトとして描画する
    SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);

    // 定数バッファを作成する
    auto* engine = GetEngineSystem();
    auto* dxCommon = engine ? engine->GetComponent<CoreEngine::DirectXCommon>() : nullptr;
    if (dxCommon) {
        CreateWaterConstantBuffer(dxCommon->GetDevice());
    }
}

void WaterPlaneObject::CreateWaterConstantBuffer(ID3D12Device* device) {
    assert(device);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kWaterCBSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&waterCBResource_));

    assert(SUCCEEDED(hr));

    waterCBGpuAddress_ = waterCBResource_->GetGPUVirtualAddress();

    // UPLOAD ヒープなのでアプリ終了まで Unmap しない
    D3D12_RANGE readRange = { 0, 0 };
    waterCBResource_->Map(0, &readRange, reinterpret_cast<void**>(&waterCBMapped_));

    // ---- フレーム定数バッファ（通常描画用 / 反射パス用） ----
    desc.Width = kFrameCBSize;
    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&frameCBResource_));
    assert(SUCCEEDED(hr));
    frameCBGpuAddress_ = frameCBResource_->GetGPUVirtualAddress();
    frameCBResource_->Map(0, &readRange, reinterpret_cast<void**>(&frameCBMapped_));
    std::memcpy(frameCBMapped_, &frameCB_, sizeof(WaterFrameConstants));

    hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&reflectionFrameCBResource_));
    assert(SUCCEEDED(hr));
    reflectionFrameCBGpuAddress_ = reflectionFrameCBResource_->GetGPUVirtualAddress();
    reflectionFrameCBResource_->Map(0, &readRange, reinterpret_cast<void**>(&reflectionFrameCBMapped_));
    std::memcpy(reflectionFrameCBMapped_, &frameCB_, sizeof(WaterFrameConstants));
}

void WaterPlaneObject::BindCustomResources(
    ID3D12GraphicsCommandList* cmdList,
    const CoreEngine::CustomShaderPipeline* pipeline) const {

    if (!cmdList || !pipeline || waterCBGpuAddress_ == 0) {
        return;
    }

    if (frameCB_.depthFadeDebugEnabled != 0) {
        const D3D12_GPU_VIRTUAL_ADDRESS selectedFrameCBGpuAddress = frameCB_.clipEnabled
            ? reflectionFrameCBGpuAddress_
            : frameCBGpuAddress_;
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::Pipeline,
            "WaterPlane BindCustomResources: b4={} b5={} reflSRV=0x{:X} depthSRV=0x{:X} sceneColorSRV=0x{:X} refractionColorSRV=0x{:X} clipEnabled={} reflectionEnabled={} depthFadeEnabled={} debugMode={}",
            waterCBGpuAddress_,
            selectedFrameCBGpuAddress,
            reflectionSRV_.ptr,
            sceneDepthSRV_.ptr,
            sceneColorSRV_.ptr,
            refractionColorSRV_.ptr,
            frameCB_.clipEnabled,
            frameCB_.reflectionEnabled,
            frameCB_.depthFadeEnabled,
            frameCB_.depthDebugViewMode);
    }

    // WaterConstants を b4 にバインドする
    int slot = pipeline->GetRootParamIndex("WaterConstants");
    if (slot >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(slot), waterCBGpuAddress_);
    }

    // WaterFrameConstants を b5 にバインドする（クリップ平面）
    int frameSlot = pipeline->GetRootParamIndex("WaterFrameConstants");
    const D3D12_GPU_VIRTUAL_ADDRESS selectedFrameCBGpuAddress = frameCB_.clipEnabled
        ? reflectionFrameCBGpuAddress_
        : frameCBGpuAddress_;
    if (frameSlot >= 0 && selectedFrameCBGpuAddress != 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(frameSlot), selectedFrameCBGpuAddress);
    }

    // 反射テクスチャ SRV をバインドする（ハンドルが有効なときのみ）
    if (reflectionSRV_.ptr != 0) {
        int reflSlot = pipeline->GetRootParamIndex("gReflectionTexture");
        if (reflSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(reflSlot), reflectionSRV_);
        }
    }

    // シーン深度 SRV をバインドする（Depth Fade 用）
    if (sceneDepthSRV_.ptr != 0) {
        int depthSlot = pipeline->GetRootParamIndex("gSceneDepth");
        if (depthSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(depthSlot), sceneDepthSRV_);
        }
    }

    // シーンカラー SRV をバインドする（水越しの背景色用）
    if (sceneColorSRV_.ptr != 0) {
        int sceneColorSlot = pipeline->GetRootParamIndex("gSceneColor");
        if (sceneColorSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(sceneColorSlot), sceneColorSRV_);
        }
    }

    // DXR 屈折カラー SRV をバインドする
    if (refractionColorSRV_.ptr != 0) {
        int refractionColorSlot = pipeline->GetRootParamIndex("gRTWaterRefractionColor");
        if (refractionColorSlot >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(refractionColorSlot), refractionColorSRV_);
        }
    }
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

void WaterPlaneObject::SetRefractionColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    refractionColorSRV_ = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetRefractionColorSRV: srv=0x{:X}",
            refractionColorSRV_.ptr);
    }
}

void WaterPlaneObject::SetActiveWaveCount(uint32_t count) {
    waterCB_.activeWaveCount = (count > kMaxWaterWaveCount) ? kMaxWaterWaveCount : count;
}

void WaterPlaneObject::SetReflectionTexture(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    reflectionSRV_ = srvHandle;
    // ハンドルが有効なときだけ反射テクスチャを有効にする
    frameCB_.reflectionEnabled = (srvHandle.ptr != 0) ? 1 : 0;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetReflectionTexture: srv=0x{:X} reflectionEnabled={}",
            reflectionSRV_.ptr,
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
    uint8_t* targetMapped = frameCB_.clipEnabled ? reflectionFrameCBMapped_ : frameCBMapped_;
    if (targetMapped) {
        std::memcpy(targetMapped, &frameCB_, sizeof(WaterFrameConstants));

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
}

void WaterPlaneObject::SetFresnelParameters(float reflectanceScale, float baseReflectance) {
    frameCB_.fresnelReflectanceScale = reflectanceScale;
    frameCB_.fresnelBaseReflectance = baseReflectance;
}

void WaterPlaneObject::SetSceneDepthSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    sceneDepthSRV_ = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetSceneDepthSRV: srv=0x{:X}",
            sceneDepthSRV_.ptr);
    }
}

void WaterPlaneObject::SetSceneColorSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    sceneColorSRV_ = srvHandle;

    if (frameCB_.depthFadeDebugEnabled != 0) {
        CoreEngine::Logger::GetInstance().Infof(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterPlane SetSceneColorSRV: srv=0x{:X}",
            sceneColorSRV_.ptr);
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

void WaterPlaneObject::ClearLightningImpacts() {
    frameCB_.lightningImpactCount = 0;
    for (auto& impact : frameCB_.lightningImpacts) {
        impact = {};
    }
}

void WaterPlaneObject::SetLightningImpactAt(
    uint32_t index,
    const CoreEngine::Vector3& impactCenter,
    float impactRadius,
    float impactIntensity,
    float chargeRadius,
    float chargeIntensity,
    float impactTime,
    float screenFlash) {
    if (index >= kMaxWaterLightningImpactCount) {
        return;
    }

    auto& impact = frameCB_.lightningImpacts[index];
    impact.center[0] = impactCenter.x;
    impact.center[1] = impactCenter.z;
    impact.radius = std::max(impactRadius, 0.0f);
    impact.intensity = std::clamp(impactIntensity, 0.0f, 1.0f);
    impact.chargeRadius = std::max(chargeRadius, 0.0f);
    impact.chargeIntensity = std::clamp(chargeIntensity, 0.0f, 1.0f);
    impact.impactTime = std::max(impactTime, 0.0f);
    impact.screenFlash = std::clamp(screenFlash, 0.0f, 1.0f);
    frameCB_.lightningImpactCount = std::max(frameCB_.lightningImpactCount, index + 1u);
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
    auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
    if (mat) { mat->SetIBLEnabled(enable); }
}

void WaterPlaneObject::UpdateUVScroll(float deltaTime) {
    // 経過時間を加算（波の位相計算に使用）
    elapsedTime_ += deltaTime;

    // UV オフセットを速度 × 時間で加算
    uvOffset_.x += scrollSpeed_.x * deltaTime;
    uvOffset_.y += scrollSpeed_.y * deltaTime;

    // 0〜1 の範囲内に折り返す（精度劣化防止）
    uvOffset_.x = std::fmod(uvOffset_.x, 1.0f);
    uvOffset_.y = std::fmod(uvOffset_.y, 1.0f);

    ApplyUVTransform();

    // 定数バッファに時間と波パラメータを書き込む
    if (waterCBMapped_) {
        waterCB_.time = elapsedTime_;
        std::memcpy(waterCBMapped_, &waterCB_, sizeof(WaterConstants));
    }
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
