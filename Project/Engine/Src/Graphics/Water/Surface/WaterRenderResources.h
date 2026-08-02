#pragma once

#include <d3d12.h>

namespace CoreEngine
{
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
        // 蓄積泡（FFTOceanFoamAccumulate.CS 出力の ping-pong 直近書き込み側）
        D3D12_GPU_DESCRIPTOR_HANDLE fftFoamSRV = { 0 };

        // ---- 大気散乱（Aerial Perspective）----
        D3D12_GPU_VIRTUAL_ADDRESS atmosphereCB = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE cameraVolumeSRV = { 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE skyViewSRV = { 0 };

        // ---- 空アンビエント（Sky Irradiance SH。水中インスキャッタの天空光）----
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSRV = { 0 };

        // ---- 空スペキュラキューブマップ（空＋雲。平面反射への雲合成用）----
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV = { 0 };

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

        /// @brief FFT Ocean 用の変位・法線・Jacobian・蓄積泡 SRV をまとめて設定する
        void SetFFTOceanTextureSRVs(
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE jacobianSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE foamSrvHandle);
    };

    /// @brief 「毎フレーム Engine 側が決める」水面描画の外部結線一式
    /// @details WaterFrameConstants のフィールドは所有者が 2 種類に分かれる。
    ///          - UI が決め、フレームをまたいで保持する値
    ///            （フレネル / Depth Fade / 光学係数 / デバッグモード / FFT 経路フラグ）
    ///          - Engine のリソース状態から毎フレーム導出する値 ← **この構造体が運ぶ分**
    ///          両者が混ざっていたため「誰がいつ書くのか」が 12 個の setter に
    ///          散っていた。境界をここで明示する。
    struct WaterFrameBinding {
        /// @brief 描画で参照する SRV / CBV 一式
        WaterRenderResources resources{};

        /// @brief 空アンビエント（Sky Irradiance SH）の輝度スケール
        float skyAmbientScale = 0.3f;

        /// @brief 大気アクティブ＋SH 生成済みか（シェーダー側の参照可否）
        bool skyAmbientEnabled = false;

        /// @brief 水面へ空気遠近感（Aerial Perspective）を適用するか
        bool aerialPerspectiveEnabled = false;

        /// @brief 空スペキュラキューブマップを反射へ合成するか
        bool skyEnvReflectionEnabled = false;

        /// @brief 深度線形化に使う描画カメラの near クリップ
        /// @details エディタの far=100000 デバッグカメラと実行時 far=1000 の
        ///          リリースカメラで値が変わるため、シェーダー定数にしてはいけない
        ///          （水柱厚さが狂い、波打ち際に段差が線として出る）。
        float cameraNearZ = 0.1f;

        /// @brief 深度線形化に使う描画カメラの far クリップ
        float cameraFarZ = 1000.0f;
    };
}
