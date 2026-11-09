#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RayTracing/Denoise/RTDenoiser.h"
#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Graphics/RayTracing/RayTracingDispatchInfo.h"
#include "Graphics/RayTracing/RayTracingHitData.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class AccelerationStructureManager;

    /// @brief RTAO の調整値（実体は CVar "r.RTAO.*"）
    struct RTAOSettings {
        float radius = 1.0f;                ///< レイの長さ [m]（これより遠い物は遮らない）
        float intensity = 1.0f;             ///< 遮りの強さ（0 = 遮らない / 1 = そのまま）
        float bias = 0.02f;                 ///< レイの始点を面から離す距離 [m]
        int   sampleCount = 1;              ///< 1 画素に撃つレイの本数
        int   newPixelSampleCount = 4;      ///< 履歴の浅い画素（現れた直後）に撃つレイの本数
        bool  halfResolutionTrace = true;   ///< ハーフ解像度で撃ち、仕上げでフル解像度へ戻す
        int   maxHistoryFrames = 32;        ///< 時間方向に重ねるフレーム数の上限
        int   atrousPassCount = 3;          ///< A-Trous のパス数（0 で空間のフィルタを掛けない）
        bool  useHistory = true;            ///< false で履歴を読まない
    };

    /// @brief レイトレーシングの環境光の遮り（RTAO）
    /// @details 画素の点から法線のまわりの半球へ短いレイを撃ち、遮られなかった割合を
    ///          RTDenoiser（1 チャンネル）でノイズ取りする。出力（フル解像度・R16_FLOAT・1 = 遮りなし）は
    ///          ライティングが SSAO の代わりに環境光へ掛ける。
    ///          毎フレーム Trace → Accumulate → Filter の順に呼ぶ。
    class RTAmbientOcclusion : public RTEffect {
    public:
        /// @brief ノイズ取りのチャンネル
        static constexpr uint32_t kDenoiseChannel = 0;

        bool Initialize(const RTEffectInitContext& init) override;
        void BeginFrame() override;
        void OnResize(uint32_t width, uint32_t height) override;

        /// @brief パイプラインとノイズ取りが使える状態か
        bool IsInitialized() const { return initialized_; }

        /// @brief CVar r.RTAO.Enabled の値
        static bool IsEnabledByCVar();

        /// @brief レイを撃ってノイズ取りの入力を書く
        /// @param guides      深度・法線（どちらもフル解像度）
        /// @param invViewProj 深度からワールド座標を戻す行列
        /// @param width,height フル解像度
        /// @param hitTables   材質の表（そろわなければアルファを調べない）
        void Trace(ID3D12GraphicsCommandList* cmdList,
            const RTDenoiseGuides& guides,
            const Matrix4x4& invViewProj,
            UINT width, UINT height,
            const RTHitTableIndices& hitTables);

        /// @brief 時間方向の蓄積（Trace の後に呼ぶ）
        void Accumulate(ID3D12GraphicsCommandList* cmdList, const RTDenoiseGuides& guides);

        /// @brief A-Trous とフル解像度の出力への仕上げ（Accumulate の後に呼ぶ）
        void Filter(ID3D12GraphicsCommandList* cmdList, const RTDenoiseGuides& guides);

        /// @brief 今フレームにレイを撃ったか
        bool IsDispatchedThisFrame() const { return dispatchedThisFrame_; }

        /// @brief 出力（フル解像度）の SRV と実体
        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV() const { return denoiser_.GetOutputSRV(kDenoiseChannel); }
        GpuResource& GetOutput() { return denoiser_.GetOutput(kDenoiseChannel); }

        /// @brief ノイズ取りの部品（デバッグ窓が中間結果を引く）
        RTDenoiser& GetDenoiser() { return denoiser_; }

        /// @brief 直近のディスパッチの診断情報
        const RayTracingDispatchInfo& GetDispatchInfo() const { return dispatchInfo_; }

        /// @brief 今の調整値（Trace の頭で CVar から取り込む）
        const RTAOSettings& GetSettings() const { return settings_; }

    private:
        /// @brief CVar（"r.RTAO.*"）の値を settings_ へ取り込む
        void SyncSettingsFromCVars();

        /// @brief settings_ からノイズ取りの調整値を作る
        RTDenoiseSettings MakeDenoiseSettings() const;

        /// @brief トレース解像度の縮小率（1 = フル解像度 / 2 = ハーフ解像度）
        UINT GetTraceScale() const { return settings_.halfResolutionTrace ? 2u : 1u; }

        GraphicsCore* dxCommon_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        RootSignatureManager rootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        BindingTable bindings_;
        bool initialized_ = false;

        RTDenoiser denoiser_;
        RTAOSettings settings_;
        RayTracingDispatchInfo dispatchInfo_{};
        bool dispatchedThisFrame_ = false;
        uint32_t frameCount_ = 0; ///< 撃った回数（乱数の列の番号）
    };
}
