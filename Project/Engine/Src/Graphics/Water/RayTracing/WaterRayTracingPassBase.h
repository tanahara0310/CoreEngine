#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>
#include <string>

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "Graphics/RayTracing/RayTracingPassBase.h"

namespace CoreEngine
{
    /// @brief DXR 水面パス共通のビュー識別子
    /// @details 以前は 3 マネージャがそれぞれ同型の enum を持っていた（3重定義）。
    ///          反射は GameView しか使わないが、識別子空間は共通でよい。
    enum class RTWaterViewID : uint32_t {
        GameView = 0,
        ReflectionView = 1,
        Count
    };

    /// @brief DXR 水面パス（屈折・反射・コースティクス）の共通基盤
    /// @details 水面固有の要素だけを持つ。水面に限らない部分は RayTracingPassBase 側にある。
    class WaterRayTracingPassBase : public RayTracingPassBase {
    public:
        /// @brief FFT Ocean の波面テクスチャ入力（3 パス共通）
        /// @details 有効判定は enabled と resolution（旧 patchLength は形骸のため撤去）。
        struct FFTOceanInput {
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            uint32_t resolution = 0;
            uint32_t enabled = 0;
        };

        /// @brief RT 側が参照する水面モデルの供給元を差し替える
        void SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider);
        /// @brief 現在の水面モデル供給元を取得
        std::shared_ptr<const IWaterSurfaceModelProvider> GetSurfaceModelProvider() const;

        /// @brief 直近ディスパッチ時の水面高さ（診断ログ用）
        float GetLastWaterHeight() const { return lastWaterHeight_; }

        /// @brief 直近ディスパッチ時の有効波数（診断ログ用）
        uint32_t GetLastActiveWaveCount() const { return lastActiveWaveCount_; }

    protected:
        /// @brief パイプライン 1 本分の構成（差分はこれだけ。残りは全パス同型）
        /// @warning 文字列・配列とも静的記憶域のものを渡すこと。
        ///          initializer_list だと一時配列がその行で寿命を終えてダングリングになる。
        struct RTWaterPipelineDesc {
            const char* ownerName = nullptr;         ///< ログ・診断に出す所有者名
            const char* outputDebugName = nullptr;   ///< 出力テクスチャのデバッグ名接頭辞
            const wchar_t* shaderPath = nullptr;     ///< DXR ライブラリ（lib_6_6）のパス
            const wchar_t* rayGenName = nullptr;
            const wchar_t* missName = nullptr;
            const wchar_t* hitGroupName = nullptr;
            const wchar_t* closestHitName = nullptr;
            const char* outputUavName = nullptr;     ///< u0 の UAV テーブル名
            std::span<const char* const> srvTableNames; ///< t1〜 の SRV テーブル名（t0 は常に gScene・静的配列を渡すこと）
            const char* constantsName = nullptr;     ///< 32bit root constants の名前
            uint32_t constantsBytes = 0;             ///< root constants のサイズ（4 の倍数）
            uint32_t payloadBytes = sizeof(float) * 2; ///< RTWaterPayload {hitT, hitFlag}
        };

        /// @brief SRV バインド 1 本分（BindAndDispatchRays へ渡す）
        struct RTWaterSrvBinding {
            const char* name;
            D3D12_GPU_DESCRIPTOR_HANDLE handle;
        };

        /// @brief 構成データからルートシグネチャ・ステートオブジェクト・シェーダーテーブルを構築する
        /// @details 失敗時はエラーログ済み。成功時は isInitialized_ = true。
        bool InitializeFromDesc(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr,
            ShaderProgramCache* shaderProgramCache,
            const RTWaterPipelineDesc& desc);

        /// @brief ディスパッチ末尾の共通処理（バインド → DispatchRays → 出力ステート遷移）
        /// @details gScene（TLAS）と gWaterSurfaceData（b1）は内部でバインドする。
        /// @param constantsBlob InitializeFromDesc で渡した constantsBytes と同サイズの定数ブロック
        /// @param finalState    出力テクスチャの最終ステート
        void BindAndDispatchRays(
            ID3D12GraphicsCommandList* cmdList,
            DispatchResources& resources,
            std::initializer_list<RTWaterSrvBinding> srvBindings,
            const void* constantsBlob,
            UINT width,
            UINT height,
            D3D12_RESOURCE_STATES finalState);

        struct alignas(16) WaterSurfaceConstants {
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            float time = 0.0f;
            uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
            // ---- Phase 3 で b0 の 3 重定義から b1 へ一本化した FFT 有効情報 ----
            // HLSL 側 RTWaterSurfaceCommon.hlsli の cbuffer WaterSurfaceData と一致必須。
            uint32_t fftOceanEnabled = 0;
            uint32_t fftOceanResolution = 0;
            // 水面メッシュの分割数（coverage 判定のメッシュ同一基準化に使う。
            // 以前はシェーダー側の kWaterMeshSubdivisions=256 ハードコードで、
            // シーン側の変更で静かに壊れる構造だった）
            float meshSubdivisions = 256.0f;
            float pad0 = 0.0f;
        };

        static_assert(sizeof(WaterSurfaceConstants) == 16 + 32 * kMaxWaterSurfaceWaveCount + 16,
            "WaterSurfaceConstants layout mismatch with RTWaterSurfaceCommon.hlsli cbuffer");

        static constexpr Cb::Field kWaterSurfaceConstantsFields[] = {
            CB_FIELD(WaterSurfaceConstants, waterHeight), CB_FIELD(WaterSurfaceConstants, activeWaveCount),
            CB_FIELD(WaterSurfaceConstants, time), CB_FIELD(WaterSurfaceConstants, simulationType),
            CB_FIELD(WaterSurfaceConstants, waves), CB_FIELD(WaterSurfaceConstants, fftOceanEnabled),
            CB_FIELD(WaterSurfaceConstants, fftOceanResolution), CB_FIELD(WaterSurfaceConstants, meshSubdivisions),
            CB_FIELD(WaterSurfaceConstants, pad0),
        };
        CB_VERIFY_LAYOUT(WaterSurfaceConstants, kWaterSurfaceConstantsFields);
        CB_BIND_HLSL(WaterSurfaceConstants, kWaterSurfaceConstantsFields, "WaterSurfaceData");

        /// @brief ディスパッチ前の共通処理（RayTracingPassBase::BeginDispatchBase の水面版）
        /// @details 水面サーフェス定数バッファのサイズを自動で渡す。
        bool BeginDispatch(
            ID3D12GraphicsCommandList* cmdList,
            UINT width,
            UINT height,
            uint32_t viewIndex,
            DispatchResources& outResources,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);

        /// @brief 診断情報の共通項目＋水面固有項目を初期化する（ディスパッチの冒頭で呼ぶ）
        void BeginDiagnostics(
            uint32_t viewIndex,
            UINT width,
            UINT height,
            const WaterSurfaceData& surfaceData,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV);

        static constexpr UINT GetSurfaceConstantBufferSize()
        {
            return (sizeof(WaterSurfaceConstants) + 255) & ~255;
        }

        /// @brief b1 の水面サーフェス定数を構築・アップロードする
        /// @param fftOceanInput FFT 有効フラグ/解像度を b1 へ載せる（Phase 3 で b0 から移動）
        WaterSurfaceConstants UploadSurfaceDataForDispatch(
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput) const;

        /// @brief provider があればそこから、無ければ fallback から surface data を決める
        const WaterSurfaceData& ResolveSurfaceDataForDispatch(
            const WaterSurfaceData& fallbackSurfaceData,
            WaterSurfaceData& outResolvedSurfaceData) const;

        std::weak_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider_;

    private:
        WaterSurfaceConstants BuildSurfaceConstants(
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput) const;
        void UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const;

        // InitializeFromDesc で受けた構成のうち、ディスパッチ時にも要るもの
        const char* outputUavName_ = nullptr;
        const char* constantsName_ = nullptr;

        /// @brief 宣言表の実体（名前は静的記憶域なので BindingTable が指し続けてよい）
        std::vector<ShaderBindingDecl> declStorage_;
        /// @brief 宣言表の添字（並びは InitializeFromDesc が決める）
        size_t slotOutputUav_ = 0;
        size_t slotScene_ = 0;
        size_t slotSrvFirst_ = 0;
        size_t slotSurfaceData_ = 0;
        size_t slotConstants_ = 0;
        uint32_t constantsBytes_ = 0;

        float lastWaterHeight_ = 0.0f;
        uint32_t lastActiveWaveCount_ = 0;
    };
}
