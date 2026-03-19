#pragma once

#include "Graphics/PostEffect/PostEffectBase.h"
#include "Math/Matrix/Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
    class LightManager;

    /// @brief G-Buffer から PBR ディファードライティングを計算するエフェクト
    /// @details LightManager（4種ライト）/ Shadow (PCF) / IBL (Irradiance+Prefiltered+BRDF LUT) を統合
    class DeferredLighting : public PostEffectBase {
    public:
        DeferredLighting() = default;
        ~DeferredLighting() override = default;

        /// @brief 初期化（ライト VP 用定数バッファを追加で生成）
        void Initialize(DirectXCommon* dxCommon) override;

        /// @brief G-Buffer SRV を入力として受け取りライティングを実行
        /// @param inputSrvHandle AlbedoAO SRV ハンドル（t0）
        void Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle) override;

        // ===== G-Buffer SRV セッター =====

        /// @brief NormalRoughness G-Buffer SRV を設定（t1）
        void SetNormalRoughnessHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)   { normalRoughnessHandle_ = handle; }

        /// @brief EmissiveMetallic G-Buffer SRV を設定（t2）
        void SetEmissiveMetallicHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)  { emissiveMetallicHandle_ = handle; }

        /// @brief WorldPosition G-Buffer SRV を設定（t3）
        void SetWorldPositionHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)     { worldPositionHandle_ = handle; }

        // ===== ライティングリソース セッター =====

        /// @brief カメラ CBV アドレスを設定（スペキュラ計算用ビュー方向）
        void SetCameraCBVAddress(D3D12_GPU_VIRTUAL_ADDRESS address) { cameraCBVAddress_ = address; }

        /// @brief シャドウマップ SRV を設定
        void SetShadowMapHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { shadowMapHandle_ = handle; }

        /// @brief ライトビュープロジェクション行列を設定（毎フレーム更新）
        /// @param lightViewProjection CPU 側 4x4 行列
        void UpdateLightViewProjection(const Matrix4x4& lightViewProjection);

        /// @brief LightManager を設定（4種ライトのバインドに使用）
        void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }

        // ===== IBL セッター =====

        /// @brief Irradiance Map SRV を設定（拡散 IBL）
        void SetIrradianceMapHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)   { irradianceMapHandle_ = handle; }

        /// @brief Prefiltered Map SRV を設定（スペキュラ IBL）
        void SetPrefilteredMapHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)  { prefilteredMapHandle_ = handle; }

        /// @brief BRDF LUT SRV を設定（スペキュラ IBL 積分用）
        void SetBRDFLUTHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)         { brdfLUTHandle_ = handle; }

    protected:
        const std::wstring& GetPixelShaderPath() const override;
        std::string GetEffectName() const override { return "DeferredLighting"; }

        /// @brief シャドウ比較サンプラーをルートシグネチャ設定に追加
        void OnConfigureRootSignature(RootSignatureConfig& config) override;

    private:
        // ===== G-Buffer SRV =====
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE emissiveMetallicHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionHandle_{};

        // ===== ライティングリソース =====
        D3D12_GPU_VIRTUAL_ADDRESS   cameraCBVAddress_   = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle_{};
        LightManager*               lightManager_       = nullptr;

        // ライトビュープロジェクション行列専用定数バッファ（毎フレーム更新）
        Microsoft::WRL::ComPtr<ID3D12Resource> lightVPBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS              lightVPCBVAddress_ = 0;

        // ===== IBL =====
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_{};
    };
}

