#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Scene/IScene.h"
#include "Math/Vector/Vector4.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    class ICamera;
}

/// 水面反射用の補助クラス。
/// ReflectionView 実行前後のカメラ状態切り替えと、
/// 水面へ渡す clip plane / reflection 結果の組み立てを担当する。
class WaterReflectionPass
{
public:
    /// @brief ReflectionView 実行前に反射カメラ状態を適用する
    /// @param mainCamera 通常描画で使用しているカメラ
    /// @param waterHeight 水面の Y 座標（ワールド空間）
    void SetupReflectionCamera(CoreEngine::ICamera* mainCamera, float waterHeight);

    /// @brief ReflectionView 実行後に元のカメラ状態へ戻す
    /// @param mainCamera 復元対象の通常描画カメラ
    void RestoreMainCamera(CoreEngine::ICamera* mainCamera);

    /// @brief 直近の Reflection 実行結果をまとめて取得する
    /// @param reflectionSrv 反射カラー SRV
    /// @param sceneDepthSrv シーン深度 SRV
    /// @param sceneColorSrv シーンカラー SRV
    /// @return 水面描画へ渡す Reflection の出力一式
    CoreEngine::ReflectionViewResult BuildResult(
        D3D12_GPU_DESCRIPTOR_HANDLE reflectionSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv) const;

    /// @brief クリップ平面を取得する（現在の水面高さに対応した値）
    const CoreEngine::Vector4& GetClipPlane() const { return clipPlane_; }

    /// @brief クリップ平面を有効にするか（Water.VS.hlsl の SV_ClipDistance0 制御用）
    bool IsClipEnabled() const { return clipEnabled_; }
    void SetClipEnabled(bool enable) { clipEnabled_ = enable; }

private:
    /// @brief カメラ位置・向きを水面平面に対して反転したビュー行列を計算する
    /// @param mainCamera 元のカメラ
    /// @param waterHeight 水面の Y 座標
    /// @return 鏡像ビュー行列
    CoreEngine::Matrix4x4 CalcReflectedViewMatrix(
        CoreEngine::ICamera* mainCamera,
        float waterHeight) const;

    /// @brief クリップ平面パラメータ（HLSL 側 SV_ClipDistance0 に渡す: dot(worldPos, clipPlane) > 0 なら描画）
    CoreEngine::Vector4 clipPlane_ = { 0.0f, 1.0f, 0.0f, 0.0f };
    bool clipEnabled_ = false;
    bool hasSavedCameraState_ = false;
    CoreEngine::Matrix4x4 savedView_{};
    CoreEngine::Vector3 savedPosition_{};
};
