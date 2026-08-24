#pragma once
#include "../RenderingTechniqueBase.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <array>
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

        /// @brief 深度復元用の View*Projection 逆行列を更新する（ビューごとに毎回呼び出し）
        /// @details gCamera はフレーム更新時に 1 回しか書かれないので、こちらを専用 CBV として毎ビュー更新する。
        /// @note ビュー種別ごとに別バッファを持つ。単一バッファだと後勝ちで両ビューが同じ行列を見てしまう。
        void UpdateDepthReconstruction(RenderViewType viewType, const Matrix4x4& invViewProj);

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

        /// @brief Water Caustics テクスチャ SRV を設定
        void SetWaterCausticsHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { waterCausticsHandle_ = handle; }

        /// @brief Water Caustics デバッグ表示設定を設定
        void SetWaterCausticsDebugSettings(const WaterCausticsDebugSettings& settings);

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
        void UpdateWaterCausticsDebugBuffer();

        /// @brief ルートパラメータを初期化時に 1 回だけ解決する
        /// @note 従来は Draw のたびに 22 回 std::map<std::string> を引いていた
        void CacheRootSlots();

        /// @brief 解決済みルートパラメータ一式
        struct Slots {
            RootSlot albedoAO;
            RootSlot normalRoughness;
            RootSlot emissiveMetallic;
            RootSlot sceneDepth;
            RootSlot camera;
            RootSlot depthReconstruction;
            RootSlot lightCounts;
            RootSlot directionalLights;
            RootSlot pointLights;
            RootSlot spotLights;
            RootSlot areaLights;
            RootSlot irradianceMap;
            RootSlot prefilteredMap;
            RootSlot brdfLUT;
            RootSlot iblParams;
            RootSlot rtShadowMask[kMaxRTShadowLights];
            RootSlot ssao;
            RootSlot waterCaustics;
            RootSlot waterCausticsDebug;
            RootSlot skyAmbient;
            RootSlot skyIrradianceSH;
            RootSlot skySpecularMap;
        };
        Slots slots_;

        // ===== 出力設定 =====
        std::string targetName_ = RenderTargetNames::SceneColor;

        // ===== ライティングリソース =====
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBVAddress_ = 0;

        // 深度復元用 View*Projection 逆行列専用定数バッファ（RenderViewType ごとに個別バッファ。
        // 同一フレーム内で GameView/ReflectionView 両方から書き込まれるため単一バッファ不可）
        static constexpr size_t kViewTypeCount = 3; // GameView / ReflectionView / CaptureView
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kViewTypeCount> depthReconstructionBuffers_;
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, kViewTypeCount> depthReconstructionCBVAddresses_{};

        // ===== IBL パラメータ =====
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS iblParamsCBVAddress_ = 0;
        Vector3 environmentRotation_ = {};
        float iblIntensity_ = 1.0f;

        // ===== RT Shadow =====
        D3D12_GPU_DESCRIPTOR_HANDLE rtShadowHandles_[kMaxRTShadowLights]{};

        // ===== SSAO =====
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoHandle_{};

        // ===== Water Caustics =====
        D3D12_GPU_DESCRIPTOR_HANDLE waterCausticsHandle_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> waterCausticsDebugBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS waterCausticsDebugCBVAddress_ = 0;
        WaterCausticsDebugSettings waterCausticsDebugSettings_{};

        // ===== 空アンビエント（大気散乱 SH。Sky Light 相当） =====
        // 有効フラグ・スケールは AtmosphereManager が持ち、Execute で毎フレーム CB へ反映する
        Microsoft::WRL::ComPtr<ID3D12Resource> skyAmbientBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS skyAmbientCBVAddress_ = 0;
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
