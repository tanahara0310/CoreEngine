#pragma once

#include <d3d12.h>

/// @brief 水面描画で使用する GPU ディスクリプタ群をまとめた構造体
/// @details WaterPlaneObject から描画リソース保持責務を切り離すための中間構造。
struct WaterRenderResources {
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE refractionColorSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE fftJacobianSRV = { 0 };

    // ---- 大気散乱（Aerial Perspective）----
    D3D12_GPU_VIRTUAL_ADDRESS atmosphereCB = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeSRV = { 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE skyViewSRV = { 0 };

    // ---- 空アンビエント（Sky Irradiance SH。水中インスキャッタの天空光）----
    D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSRV = { 0 };

    /// @brief 反射テクスチャが接続済みか返す
    bool HasReflectionTexture() const;

    /// @brief シーン深度テクスチャが接続済みか返す
    bool HasSceneDepth() const;

    /// @brief シーンカラーテクスチャが接続済みか返す
    bool HasSceneColor() const;

    /// @brief 屈折結果テクスチャが接続済みか返す
    bool HasRefractionColor() const;

    /// @brief FFT Ocean 用の主要 SRV が接続済みか返す
    bool HasFFTOceanTextureSRVs() const;

    /// @brief 大気散乱（Aerial Perspective）リソース一式が接続済みか返す
    bool HasAtmosphere() const;

    /// @brief FFT Ocean 用の変位・法線・Jacobian SRV をまとめて設定する
    void SetFFTOceanTextureSRVs(
        D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle);
};
