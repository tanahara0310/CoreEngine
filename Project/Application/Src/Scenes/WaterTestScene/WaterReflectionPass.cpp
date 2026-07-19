#include "pch.h"
#include "WaterReflectionPass.h"

#include "Camera/ICamera.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <DirectXMath.h>
#include <cmath>

using namespace CoreEngine;
using namespace DirectX;

void WaterReflectionPass::SetupReflectionCamera(ICamera* mainCamera, float waterHeight)
{
    if (!mainCamera) {
        viewOverrideApplied_ = false;
        return;
    }

    // ReflectionView 実行中に水面より上だけを描画するため、クリップ平面を更新する。
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

    Matrix4x4 reflectedView = CalcReflectedViewMatrix(mainCamera, waterHeight);

    Vector3 reflectedPos = mainCamera->GetPosition();
    reflectedPos.y = 2.0f * waterHeight - reflectedPos.y;

    // 水面より下のジオメトリが反射へ映り込まないよう、近クリップ面を水面へ傾けた
    // 斜交射影を併せて適用する。
    Matrix4x4 obliqueProjection = CalcObliqueClippedProjection(
        mainCamera->GetProjectionMatrix(), reflectedView, waterHeight);

    // ICamera のビュー差し替え API で反射ビューを適用する。
    // 以前は dynamic_cast<Camera*> 経由だったため、エディタの DebugCamera（Camera 非継承）では
    // キャストが失敗してミラーが無言で不適用になり、反射テクスチャに
    // 「水なしの通常シーン」が入って水面へ加算される不具合があった。
    viewOverrideApplied_ = mainCamera->BeginViewOverride(reflectedView, reflectedPos, &obliqueProjection);
    if (!viewOverrideApplied_ && !overrideUnsupportedLogged_) {
        overrideUnsupportedLogged_ = true;
        Logger::GetInstance().Warnf(
            CoreEngine::LogCategory::Graphics,
            CoreEngine::LogSubCategory::RenderTarget,
            "WaterReflectionPass: カメラがビュー差し替え（BeginViewOverride）に対応していないため、"
            "反射ビューがミラー化されずに描画されます");
    }
}

void WaterReflectionPass::RestoreMainCamera(ICamera* mainCamera)
{
    clipEnabled_ = false;
    if (mainCamera && viewOverrideApplied_) {
        mainCamera->EndViewOverride();
    }
    viewOverrideApplied_ = false;
}

Matrix4x4 WaterReflectionPass::CalcObliqueClippedProjection(
    const Matrix4x4& projection,
    const Matrix4x4& reflectedView,
    float waterHeight) const
{
    // ワールド空間のクリップ平面: dot(p, W) >= 0 ⇔ p.y >= waterHeight（水面より上だけ残す）
    const float worldPlane[4] = { 0.0f, 1.0f, 0.0f, -waterHeight };

    // 行ベクトル規約（v_view = v_world * View）では、ビュー空間の平面係数は
    // C_i = Σ_j Inverse(View)[i][j] * W_j で得られる（dot(v_view, C) = dot(v_world, W)）。
    const Matrix4x4 invView = CoreEngine::MathCore::Matrix::Inverse(reflectedView);
    float viewPlane[4];
    for (int i = 0; i < 4; ++i) {
        viewPlane[i] = invView.m[i][0] * worldPlane[0]
                     + invView.m[i][1] * worldPlane[1]
                     + invView.m[i][2] * worldPlane[2]
                     + invView.m[i][3] * worldPlane[3];
    }

    // Lengyel の oblique near-plane clipping（D3D の z∈[0,1] へ合わせた形）。
    // 射影行列の z 出力列を u*C に置き換えると、平面上で z'=0（近クリップ）、
    // 平面法線側の遠方コーナー Q で z'=w（遠クリップ）になる。
    const auto sgn = [](float v) { return (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f); };
    const float xScale = projection.m[0][0];
    const float yScale = projection.m[1][1];
    const float zA = projection.m[2][2];
    const float zB = projection.m[3][2];
    if (std::fabs(xScale) < 1.0e-6f || std::fabs(yScale) < 1.0e-6f || std::fabs(zB) < 1.0e-6f) {
        return projection;
    }

    // 遠クリップ面上の、平面法線側コーナーのビュー空間同次座標
    const float qX = sgn(viewPlane[0]) / xScale;
    const float qY = sgn(viewPlane[1]) / yScale;
    const float qZ = 1.0f;
    const float qW = (1.0f - zA) / zB; // = 1 / farClip

    const float dotQC = qX * viewPlane[0] + qY * viewPlane[1] + qZ * viewPlane[2] + qW * viewPlane[3];
    if (std::fabs(dotQC) < 1.0e-6f) {
        // 平面がカメラとほぼ平行など退化した場合は通常の射影のまま描画する
        return projection;
    }

    const float u = 1.0f / dotQC;
    Matrix4x4 result = projection;
    result.m[0][2] = u * viewPlane[0];
    result.m[1][2] = u * viewPlane[1];
    result.m[2][2] = u * viewPlane[2];
    result.m[3][2] = u * viewPlane[3];
    return result;
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
