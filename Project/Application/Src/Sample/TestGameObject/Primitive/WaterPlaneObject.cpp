#include "WaterPlaneObject.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Math/MathCore.h"
#include <cmath>
#include <cstring>
#include <cassert>

// ---- 定数バッファのサイズは 256 バイトアライメント ----
static constexpr UINT kWaterCBSize =
(sizeof(WaterConstants) + 255) & ~255u;

static constexpr UINT kFrameCBSize =
(sizeof(WaterFrameConstants) + 255) & ~255u;

WaterPlaneObject::WaterPlaneObject(float size, uint32_t resolution,
    const std::string& albedoTextureName)
    : size_(size)
    , resolution_(resolution)
    , albedoTextureName_(albedoTextureName)
    , scrollSpeed_({ 0.03f, 0.01f })
    , uvTiling_({ 4.0f, 4.0f })
    , uvOffset_({ 0.0f, 0.0f }) {

    // デフォルトの波パラメータを設定（4 本の Gerstner Wave を重ね合わせる）
    waterCB_.waves[0] = { {  1.0f,  0.0f }, 0.5f, 12.0f, 1.8f, 0.5f };
    waterCB_.waves[1] = { {  0.7f,  0.7f }, 0.3f,  8.0f, 2.2f, 0.4f };
    waterCB_.waves[2] = { { -0.5f,  0.8f }, 0.2f,  6.0f, 2.6f, 0.3f };
    waterCB_.waves[3] = { {  0.2f, -0.9f }, 0.1f,  4.0f, 3.0f, 0.2f };
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

    // ---- フレーム定数バッファ（クリップ平面） ----
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
    // 初期値を書き込む
    std::memcpy(frameCBMapped_, &frameCB_, sizeof(WaterFrameConstants));
}

void WaterPlaneObject::BindCustomResources(
    ID3D12GraphicsCommandList* cmdList,
    const CoreEngine::CustomShaderPipeline* pipeline) const {

    if (!cmdList || !pipeline || waterCBGpuAddress_ == 0) {
        return;
    }

    // WaterConstants を b4 にバインドする
    int slot = pipeline->GetRootParamIndex("WaterConstants");
    if (slot >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(slot), waterCBGpuAddress_);
    }

    // WaterFrameConstants を b5 にバインドする（クリップ平面）
    int frameSlot = pipeline->GetRootParamIndex("WaterFrameConstants");
    if (frameSlot >= 0 && frameCBGpuAddress_ != 0) {
        cmdList->SetGraphicsRootConstantBufferView(
            static_cast<UINT>(frameSlot), frameCBGpuAddress_);
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
}

void WaterPlaneObject::SetNormalMapTextureName(const std::string& fileName) {
    auto loaded = CoreEngine::TextureManager::GetInstance().Load(fileName);
    if (GetModel() && loaded.gpuHandle.ptr != 0) {
        GetModel()->SetNormalMapOverride(loaded.gpuHandle);
        GetModel()->GetMaterial()->SetNormalMapEnabled(true);
    }
}

void WaterPlaneObject::SetScrollSpeed(const CoreEngine::Vector2& speed) {
    scrollSpeed_ = speed;
}

void WaterPlaneObject::SetUVTiling(const CoreEngine::Vector2& tiling) {
    uvTiling_ = tiling;
}

void WaterPlaneObject::SetWave(uint32_t index, const WaveParams& wave) {
    if (index < 4) {
        waterCB_.waves[index] = wave;
    }
}

void WaterPlaneObject::SetReflectionTexture(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    reflectionSRV_ = srvHandle;
    // ハンドルが有効なときだけ反射テクスチャを有効にする
    frameCB_.reflectionEnabled = (srvHandle.ptr != 0) ? 1 : 0;
}

void WaterPlaneObject::SetClipPlane(const CoreEngine::Vector4& clipPlane, bool enable) {
    frameCB_.clipPlane[0] = clipPlane.x;
    frameCB_.clipPlane[1] = clipPlane.y;
    frameCB_.clipPlane[2] = clipPlane.z;
    frameCB_.clipPlane[3] = clipPlane.w;
    frameCB_.clipEnabled  = enable ? 1 : 0;
}

void WaterPlaneObject::UpdateFrameConstants() {
    if (frameCBMapped_) {
        std::memcpy(frameCBMapped_, &frameCB_, sizeof(WaterFrameConstants));
    }
}

void WaterPlaneObject::SetFresnelAlpha(float minAlpha, float maxAlpha) {
    frameCB_.fresnelMinAlpha = minAlpha;
    frameCB_.fresnelMaxAlpha = maxAlpha;
}

void WaterPlaneObject::SetSceneDepthSRV(D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    sceneDepthSRV_ = srvHandle;
}

void WaterPlaneObject::SetDepthFade(float absorptionCoeff, bool enabled) {
    frameCB_.absorptionCoeff  = absorptionCoeff;
    frameCB_.depthFadeEnabled = enabled ? 1 : 0;
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
    auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
    if (mat) { mat->SetIBLEnabled(enable); }
}

void WaterPlaneObject::SetNormalMapEnabled(bool enable) {
    auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
    if (mat) { mat->SetNormalMapEnabled(enable); }
}

void WaterPlaneObject::SetAlbedoTextureEnabled(bool enable) {
    if (enable && !albedoTextureName_.empty()) {
        // アルベドテクスチャをロードして texture_ に設定する
        texture_ = CoreEngine::TextureManager::GetInstance().Load(albedoTextureName_);
    } else {
        // ptr を 0 にすることで white1x1.png フォールバックになり、
        // albedo = color.rgb * (1,1,1) = color.rgb（ベースカラーのみ）となる
        texture_.gpuHandle = { 0 };
        texture_.texture.Reset();
    }
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
