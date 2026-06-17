#include "pch.h"
#include "WaterReflectionPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Camera/ICamera.h"
#include "Camera/Release/Camera.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <DirectXMath.h>
#include <cmath>

using namespace CoreEngine;
using namespace DirectX;

void WaterReflectionPass::Initialize(DirectXCommon* dxCommon, int offscreenIndex)
{
    dxCommon_ = dxCommon;

    Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::RenderTarget,
        "WaterReflectionPass Initialize: offscreenIndex={} size={}x{}",
        offscreenIndex,
        dxCommon ? dxCommon->GetClientWidth() : 0,
        dxCommon ? dxCommon->GetClientHeight() : 0);

    // 反射専用のオフスクリーン RTT を確保する
    reflectionRT_ = std::make_unique<OffscreenRenderTarget>();
    reflectionRT_->Initialize(dxCommon, offscreenIndex);
    reflectionRT_->SetUseDepthBuffer(true);

    auto* device = dxCommon ? dxCommon->GetDevice() : nullptr;
    auto* descriptorManager = dxCommon ? dxCommon->GetDescriptorManager() : nullptr;
    if (device && descriptorManager) {
        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = static_cast<UINT64>(dxCommon->GetClientWidth());
        depthDesc.Height = static_cast<UINT>(dxCommon->GetClientHeight());
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        reflectionDepthResource_ = ResourceFactory::CreateTextureResource(
            Microsoft::WRL::ComPtr<ID3D12Device>(device),
            depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue);

        reflectionDepthDsvHandle_ = descriptorManager->AllocateDSVHandle("WaterReflectionDepthDSV");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            reflectionDepthResource_.Get(),
            &dsvDesc,
            reflectionDepthDsvHandle_.cpuHandle);

        reflectionRT_->SetDepthStencilHandle(reflectionDepthDsvHandle_.cpuHandle);

    }

    // 反射パスではクリアを有効にする
    const float clearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    dxCommon->SetOffScreenTargetClearColor(static_cast<uint32_t>(offscreenIndex), clearColor);
}

void WaterReflectionPass::Render(
    ID3D12GraphicsCommandList* cmdList,
    RenderManager* renderManager,
    ICamera* mainCamera,
    float waterHeight)
{
    if (!cmdList || !renderManager || !mainCamera || !reflectionRT_) {
        return;
    }

    // ---- 1. クリップ平面を更新する（水面より上だけを描画） ----
    // dot(worldPos, clipPlane) > 0 のフラグメントのみ通過させる
    clipPlane_ = { 0.0f, 1.0f, 0.0f, -waterHeight };
    clipEnabled_ = true;

    Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::RenderTarget,
        "WaterReflectionPass Render: waterHeight={:.3f} cameraPos=({:.3f}, {:.3f}, {:.3f}) clipPlane=({:.3f}, {:.3f}, {:.3f}, {:.3f})",
        waterHeight,
        mainCamera->GetPosition().x,
        mainCamera->GetPosition().y,
        mainCamera->GetPosition().z,
        clipPlane_.x,
        clipPlane_.y,
        clipPlane_.z,
        clipPlane_.w);

    // ---- 2. 鏡像ビュー行列を計算する ----
    Matrix4x4 reflectedView = CalcReflectedViewMatrix(mainCamera, waterHeight);

    // ---- 3. カメラに鏡像ビュー行列を適用し GPU へ転送する ----
    // 元の状態を退避しておき描画後に復元する
    const Matrix4x4 savedView = mainCamera->GetViewMatrix();
    const Vector3 savedPos    = mainCamera->GetPosition();

    // 反射カメラ位置（Y 軸を水面に対して反転）
    Vector3 reflectedPos = savedPos;
    reflectedPos.y = 2.0f * waterHeight - savedPos.y;

    // Camera インターフェース経由で view 行列を差し替える
    // （ICamera の SetParameters 経由でパラメータを渡す）
    // 内部的に SetViewMatrix + SetTranslate を提供しているため CameraParameters に書き込む
    CameraParameters params = mainCamera->GetParameters();
    const CameraParameters savedParams = params;

    // ICamera には SetViewMatrix の統一 API が無いため
    // GetParameters/SetParameters で外部ビュー行列フラグを経由する方針はとらず、
    // RenderManager::SetCamera は ICamera ポインタのまま参照するため
    // ここでは mainCamera の view 行列を一時的に差し替えて TransferMatrix を呼ぶ。
    //
    // Camera クラスが SetViewMatrix(Matrix4x4) を持つため、
    // static_cast ではなく ICamera の pure virtual には存在しないが
    // 実際の Camera インスタンスにキャストして使用する。
    // より安全な方法は WaterReflectionCamera を別途作ることだが、
    // ここでは ICamera の TransferMatrix() が最終的に GPU へ転送することを利用する。

    // ---- RenderManager に反射ビューを設定 ----
    // RenderManager::SetCamera は ICamera* を保持するだけなので
    // mainCamera の view 行列を直接変更して TransferMatrix() を呼び直す。
    // ICamera::SetViewMatrix は基底クラスには無いため、dynamic_cast で試みる。
    auto* castableCamera = dynamic_cast<CoreEngine::Camera*>(mainCamera);
    if (castableCamera) {
        castableCamera->SetViewMatrix(reflectedView);
        castableCamera->SetTranslate(reflectedPos);
        castableCamera->TransferMatrix();
    }

    // ---- 4. 反射 RTT に切り替えてシーンを描画 ----
    reflectionRT_->Begin(cmdList);

    // RenderManager に描画させる（drawQueue_ は PrepareRender で積まれた内容を使い回す）
    // ※水面オブジェクト自体は水面より上にないため映り込まない（クリップ平面で除去される）
    renderManager->SetCommandList(cmdList);
    renderManager->DrawGeometryPass();

    reflectionRT_->End(cmdList);

    // ---- 5. カメラを元に戻す ----
    clipEnabled_ = false;
    if (castableCamera) {
        castableCamera->SetViewMatrix(savedView);
        castableCamera->SetTranslate(savedPos);
        castableCamera->TransferMatrix();
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionPass::GetReflectionSRV() const
{
    if (!reflectionRT_) {
        return { 0 };
    }
    return reflectionRT_->GetSRVHandle();
}

Matrix4x4 WaterReflectionPass::CalcReflectedViewMatrix(
    ICamera* mainCamera,
    float waterHeight) const
{
    // 水面平面 Y = waterHeight に対してカメラビュー行列を反転する
    // XMMatrixReflect を使用して反射行列を計算し、ビュー行列に乗算する

    // 水面平面: Y 軸正方向の法線、水面高さ = waterHeight
    // 平面方程式: 0*x + 1*y + 0*z + (-waterHeight) = 0
    XMVECTOR plane = XMVectorSet(0.0f, 1.0f, 0.0f, -waterHeight);
    XMMATRIX reflectMtx = XMMatrixReflect(plane);

    // mainCamera のビュー行列を XMMATRIX に変換
    const Matrix4x4& view = mainCamera->GetViewMatrix();
    XMMATRIX viewMtx = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&view));

    // 反射行列 × ビュー行列
    XMMATRIX reflectedView = XMMatrixMultiply(reflectMtx, viewMtx);

    // Matrix4x4 に変換して返す
    Matrix4x4 result;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result), reflectedView);
    return result;
}
