#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/RHI/Resource/PerFrameConstants.h"
#include "Camera/CameraStructs.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <d3d12.h>

namespace CoreEngine
{
    class LightManager;

    /// @brief Deferred Lighting レンダリング技術
    /// @details GBufferからPBRディファードライティングを計算
    ///          LightManager（4種ライト）/ RT シャドウ / 空アンビエント（大気散乱）を統合
    class DeferredLightingTechnique : public RenderingTechniqueBase {
    public:
        static constexpr uint32_t kMaxRTShadowLights = 4;

        /// @brief コースティクスのデバッグ表示＋水中ライティング設定（HLSL 側 WaterCausticsDebug と一致させること）
        /// @details waterVolumeEnabled=1 のとき、水中ピクセルのメインライト直接光を
        ///          コースティクス（完全な透過直接光）で置換し、アンビエントを Beer–Lambert で
        ///          減衰させる。これにより海底が「水なしの直射日光＋コースティクス加算」を
        ///          受ける二重計上を排除する。RT コースティクスが有効なフレームのみ立てる。
        struct WaterCausticsDebugSettings {
            uint32_t debugViewMode = 0;
            float debugDisplayScale = 1.0f;
            uint32_t waterVolumeEnabled = 0;
            float waterHeight = 0.0f;
            float regionCenterXZ[2] = {};
            float regionHalfExtentXZ[2] = {};
            float absorptionCoeff[3] = {};
            float padding0 = 0.0f;
        };

        DeferredLightingTechnique() = default;
        ~DeferredLightingTechnique() override = default;

        void Initialize(GraphicsCore* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;

        // ===== カメラ・ライティングリソース セッター =====

        /// @brief カメラ CBV アドレスを設定（スペキュラ計算用ビュー方向）
        void SetCameraCBVAddress(D3D12_GPU_VIRTUAL_ADDRESS address) { cameraCBVAddress_ = address; }

        /// @brief フォールバック用のカメラ位置を控える（有効なカメラがあるフレームだけ呼ぶ）
        /// @details カメラ不在フレーム（シーン構築中など）は cameraCBVAddress_ が 0 になる。
        ///          そのフレームでも gCamera へ必ず有効なアドレスを差せるよう、
        ///          直近の位置を控え、そのフレームの UploadRing に置いて差す。
        void UpdateFallbackCameraPosition(const Vector3& worldPosition) { fallbackCamera_.Set(CameraForGPU{ worldPosition }); }

        /// @brief 深度復元用の View*Projection 逆行列を設定する（ビューを描く前に毎回呼ぶ）
        /// @details GPU へは Execute でそのフレームの UploadRing に置く。設定し直すたびに置き直すので、
        ///          1 フレームに複数のビューを描いても、それぞれのビューの行列を読む。
        void SetDepthReconstruction(const Matrix4x4& invViewProj) { depthReconstruction_.Set(invViewProj); }

        // ===== SSAO セッター =====

        /// @brief SSAO テクスチャ SRV を設定
        void SetSSAOHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { ssaoHandle_ = handle; }

        /// @brief Water Caustics テクスチャ SRV を設定
        void SetWaterCausticsHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { waterCausticsHandle_ = handle; }

        /// @brief Water Caustics デバッグ表示設定を設定
        void SetWaterCausticsDebugSettings(const WaterCausticsDebugSettings& settings) { waterCausticsDebug_.Set(settings); }

        /// @brief 水中ライティングの設定（このフレームで水中の点を照らすのに使った値）
        const WaterCausticsDebugSettings& GetWaterCausticsDebugSettings() const { return waterCausticsDebug_.Get(); }

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
        /// @brief 宣言表（DeferredLightingBind::kDecls）を初期化時に 1 回だけ解決する
        /// @note 契約違反（必須リソースの不在・種別違い）はここで throw される
        void ResolveBindings();

        /// @brief 解決済みルートパラメータ表。描画中は添字でしか触らない
        BindingTable bindings_;

        // ===== 出力設定 =====
        std::string targetName_ = RenderTargetNames::SceneColor;

        // ===== ライティングリソース =====
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBVAddress_ = 0;

        // カメラ不在フレーム用のフォールバック CBV。
        // 未設定のルート CBV は未定義の GPU 仮想アドレスを指すため、シェーダが gCamera を
        // 読んだ時点でページフォルト（＝デバイスロスト）になり得る。踏むかどうかは
        // ドライバ任せなので、環境によって落ちたり落ちなかったりする。
        // 中身の正しさより「常に有効なアドレスが差さっていること」が目的。
        PerFrameConstants<CameraForGPU> fallbackCamera_;

        // 深度復元用 View*Projection 逆行列
        PerFrameConstants<Matrix4x4> depthReconstruction_;

        // ===== RT Shadow =====
        D3D12_GPU_DESCRIPTOR_HANDLE rtShadowHandles_[kMaxRTShadowLights]{};

        // ===== SSAO =====
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoHandle_{};

        // ===== Water Caustics =====
        D3D12_GPU_DESCRIPTOR_HANDLE waterCausticsHandle_{};
        PerFrameConstants<WaterCausticsDebugSettings> waterCausticsDebug_;

        // ===== 空アンビエント（大気散乱 SH。Sky Light 相当） =====
        // 有効フラグ・スケールは AtmosphereManager が持ち、Execute で毎フレーム UploadRing に置く
        /// @brief 空アンビエント（Sky Irradiance SH / 空スペキュラ IBL）の有効状態と強度
        struct SkyAmbientParams {
            uint32_t enabled = 0;
            float scale = 0.0f;
            uint32_t specularEnabled = 0; ///< 1 = 空スペキュラIBL（空＋雲キューブマップの環境反射）有効
            float padding = 0.0f;
        };

        static constexpr Cb::Field kSkyAmbientParamsFields[] = {
            CB_FIELD(SkyAmbientParams, enabled), CB_FIELD(SkyAmbientParams, scale),
            CB_FIELD(SkyAmbientParams, specularEnabled), CB_FIELD(SkyAmbientParams, padding),
        };
        CB_VERIFY_LAYOUT(SkyAmbientParams, kSkyAmbientParamsFields);
        CB_BIND_HLSL(SkyAmbientParams, kSkyAmbientParamsFields, "gSkyAmbient");
    };
}
