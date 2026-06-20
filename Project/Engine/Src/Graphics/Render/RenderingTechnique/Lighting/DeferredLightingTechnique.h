#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
    class LightManager;

    /// @brief Deferred Lighting レンダリング技術
    /// @details GBufferからPBRディファードライティングを計算
    ///          LightManager（4種ライト）/ Shadow (PCF) / IBL (Irradiance+Prefiltered+BRDF LUT) を統合
    class DeferredLightingTechnique : public RenderingTechniqueBase {
    public:
        static constexpr uint32_t kMaxRTShadowLights = 4;

        DeferredLightingTechnique() = default;
        ~DeferredLightingTechnique() override = default;

        void Initialize(DirectXCommon* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;

        // ===== カメラ・ライティングリソース セッター =====

        /// @brief カメラ CBV アドレスを設定（スペキュラ計算用ビュー方向）
        void SetCameraCBVAddress(D3D12_GPU_VIRTUAL_ADDRESS address) { cameraCBVAddress_ = address; }

        /// @brief ライトビュープロジェクション行列を設定（毎フレーム更新）
        void UpdateLightViewProjection(const Matrix4x4& lightViewProjection);

        // ===== IBL セッター =====

        /// @brief 環境マップ XYZ 回転角度を設定（ラジアン）
        void SetEnvironmentRotation(const Vector3& rotation) { environmentRotation_ = rotation; }

        /// @brief IBL 強度を設定
        void SetIBLIntensity(float intensity) { iblIntensity_ = intensity; }

        /// @brief IBL パラメータを GPU バッファに書き込む（毎フレーム呼び出し）
        void UpdateIBLParams();

        // ===== SSAO セッター =====

        /// @brief SSAO テクスチャ SRV を設定
        void SetSSAOHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { ssaoHandle_ = handle; }

        /// @brief RT シャドウマスク SRV を設定（DXR レイトレーシングシャドウ結果）
        /// @param handle  SRV ハンドル（無効時は {} を渡す）
        /// @param lightIndex  ディレクショナルライトのインデックス（0〜3）
        void SetRTShadowHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle, uint32_t lightIndex = 0) {
            if (lightIndex < kMaxRTShadowLights) {
                rtShadowHandles_[lightIndex] = handle;
            }
        }

        /// @brief 出力先レンダーターゲット名を設定
        void SetRenderTargetName(const std::string& name) { targetName_ = name; }

    protected:
        std::string GetTechniqueName() const override { return "DeferredLighting"; }
        const std::wstring& GetPixelShaderPath() const override;
        void OnConfigureRootSignature(RootSignatureConfig& config) override;

        /// @brief 常時有効な技術
        bool IsAlwaysEnabled() const override { return true; }

    private:
        void CreateConstantBuffers();

        // ===== 出力設定 =====
        std::string targetName_ = RenderTargetNames::SceneColor;

        // ===== ライティングリソース =====
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBVAddress_ = 0;

        // ライトビュープロジェクション行列専用定数バッファ（毎フレーム更新）
        Microsoft::WRL::ComPtr<ID3D12Resource> lightVPBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS lightVPCBVAddress_ = 0;

        // ===== IBL パラメータ =====
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS iblParamsCBVAddress_ = 0;
        Vector3 environmentRotation_ = {};
        float iblIntensity_ = 1.0f;

        // ===== RT Shadow =====
        D3D12_GPU_DESCRIPTOR_HANDLE rtShadowHandles_[kMaxRTShadowLights]{};

        // ===== SSAO =====
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoHandle_{};
    };
}
