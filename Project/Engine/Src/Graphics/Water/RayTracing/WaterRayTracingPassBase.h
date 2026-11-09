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
#include "Graphics/RayTracing/RayTracingPassBase.h"
#include "Graphics/RayTracing/RTHitShading.h"
#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{

    struct RenderContext;

    /// @brief 水面のレイトレの効果を作る組の名前（RenderDomainContext::EnsureWaterInitialized が作る）
    inline constexpr const char* kWaterRTEffectGroup = "Water";

    /// @brief DXR 水面パス共通のビュー識別子
    enum class RTWaterViewID : uint32_t {
        GameView = 0,
        Count
    };

    /// @brief DXR 水面パス（屈折・反射・コースティクス）の共通基盤
    /// @details 水面固有の要素だけを持つ。水面に限らない部分は RayTracingPassBase 側にある。
    class WaterRayTracingPassBase : public RayTracingPassBase, public RTEffect {
    public:
        /// @brief 別スレッドで作っているパイプラインの完成を待つ
        bool WaitForPipelines() override { return WaitForPipeline(); }

        /// @brief FFT の海の波面のテクスチャ入力（WaterSurfaceData::fftOcean）
        using FFTOceanInput = WaterFFTOceanInput;

        /// @brief 直近ディスパッチ時の水面高さ（診断ログ用）
        float GetLastWaterHeight() const { return lastWaterHeight_; }

        /// @brief 直近ディスパッチ時の有効波数（診断ログ用）
        uint32_t GetLastActiveWaveCount() const { return lastActiveWaveCount_; }

        /// @brief b1 の水面サーフェス定数（RTWaterSurfaceCommon.hlsli の cbuffer WaterSurfaceData）
        struct alignas(16) WaterSurfaceConstants {
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            float time = 0.0f;
            uint32_t simulationType = static_cast<uint32_t>(WaterWaveModelType::Gerstner);
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
            // ---- Phase 3 で b0 の 3 重定義から b1 へ一本化した FFT 有効情報 ----
            // HLSL 側 RTWaterSurfaceCommon.hlsli の cbuffer WaterSurfaceData と一致必須。
            uint32_t fftOceanEnabled = 0;
            uint32_t fftOceanResolution = 0;
            // 水面メッシュの分割数（coverage 判定のメッシュ同一基準化に使う。
            // 以前はシェーダー側の kWaterMeshSubdivisions=256 ハードコードで、
            // シーン側の変更で静かに壊れる構造だった）
            float meshSubdivisions = 256.0f;
            uint32_t regionValid = 0;                     ///< 0 = 水面メッシュの範囲が分からない
            // FFT カスケードごとの平均二乗傾斜（x・z の傾きの二乗和の平均）
            float cascadeMeanSquareSlope[3] = { 0.0f, 0.0f, 0.0f };
            float fineWaveMeanSquareSlope = 0.0f;         ///< カスケードより細かい波の平均二乗傾斜
            float regionCenterXZ[2] = { 0.0f, 0.0f };     ///< 水面メッシュのワールド XZ 範囲の中心
            float regionHalfExtentXZ[2] = { 0.0f, 0.0f }; ///< 水面メッシュのワールド XZ 範囲の半分の大きさ
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };   ///< 波群エンベロープの位相のずれ [rad]
            float pad2 = 0.0f;
            // ---- 岸の細かい格子（WaterSurfaceData の同名の値）----
            uint32_t shoreMeshEnabled = 0;
            float shoreMeshSize = 0.0f;
            float shoreMeshOriginXZ[2] = { 0.0f, 0.0f };
            float shoreMeshCellSize = 0.0f;
            float shoreMeshBlendWidth = 0.0f;
            uint32_t shoreMeshDisplacementSrvIndex = 0;
            float pad3 = 0.0f;
            // ---- 海の範囲マップ（WaterSeaMapBinding の同名の値）----
            float seaMapOriginXZ[2] = { 0.0f, 0.0f };
            float seaMapTexelsPerMeter = 0.0f;
            uint32_t seaMapSrvIndex = 0;
            uint32_t seaMapResolution[2] = { 0, 0 };
            float seaMapDepthOffset = 0.0f;
            float seaMapInvWaveHeight = 0.0f;
        };

        static_assert(sizeof(WaterSurfaceConstants) == 16 + 32 * kMaxWaterSurfaceWaveCount + 128,
            "WaterSurfaceConstants layout mismatch with RTWaterSurfaceCommon.hlsli cbuffer");

        static constexpr Cb::Field kWaterSurfaceConstantsFields[] = {
            CB_FIELD(WaterSurfaceConstants, waterHeight), CB_FIELD(WaterSurfaceConstants, activeWaveCount),
            CB_FIELD(WaterSurfaceConstants, time), CB_FIELD(WaterSurfaceConstants, simulationType),
            CB_FIELD(WaterSurfaceConstants, waves), CB_FIELD(WaterSurfaceConstants, fftOceanEnabled),
            CB_FIELD(WaterSurfaceConstants, fftOceanResolution), CB_FIELD(WaterSurfaceConstants, meshSubdivisions),
            CB_FIELD(WaterSurfaceConstants, regionValid), CB_FIELD(WaterSurfaceConstants, cascadeMeanSquareSlope),
            CB_FIELD(WaterSurfaceConstants, fineWaveMeanSquareSlope), CB_FIELD(WaterSurfaceConstants, regionCenterXZ),
            CB_FIELD(WaterSurfaceConstants, regionHalfExtentXZ), CB_FIELD(WaterSurfaceConstants, waveGroupPhase),
            CB_FIELD(WaterSurfaceConstants, pad2), CB_FIELD(WaterSurfaceConstants, shoreMeshEnabled),
            CB_FIELD(WaterSurfaceConstants, shoreMeshSize), CB_FIELD(WaterSurfaceConstants, shoreMeshOriginXZ),
            CB_FIELD(WaterSurfaceConstants, shoreMeshCellSize), CB_FIELD(WaterSurfaceConstants, shoreMeshBlendWidth),
            CB_FIELD(WaterSurfaceConstants, shoreMeshDisplacementSrvIndex), CB_FIELD(WaterSurfaceConstants, pad3),
            CB_FIELD(WaterSurfaceConstants, seaMapOriginXZ),
            CB_FIELD(WaterSurfaceConstants, seaMapTexelsPerMeter), CB_FIELD(WaterSurfaceConstants, seaMapSrvIndex),
            CB_FIELD(WaterSurfaceConstants, seaMapResolution), CB_FIELD(WaterSurfaceConstants, seaMapDepthOffset),
            CB_FIELD(WaterSurfaceConstants, seaMapInvWaveHeight),
        };
        CB_VERIFY_LAYOUT(WaterSurfaceConstants, kWaterSurfaceConstantsFields);
        CB_BIND_HLSL(WaterSurfaceConstants, kWaterSurfaceConstantsFields, "WaterSurfaceData");

        /// @brief b1 の水面サーフェス定数を組み立てる
        /// @details 水面の高さを聞く計算シェーダー（WaterHeightQueryManager）も同じ b1 を読む
        static WaterSurfaceConstants BuildSurfaceConstants(
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput);

        /// @brief 水面のレイトレのパスが撃つ前に集める、このフレームの入力
        struct FrameInput {
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV{};  ///< 水面を合成する前の SceneColor
            Matrix4x4 viewProjection{};
            Vector3 cameraPosition{};
            UINT width = 0;
            UINT height = 0;
            FFTOceanInput fftOceanInput{};
        };

        /// @brief このフレームの入力を集める
        /// @param debugLabel 撃たなかったときの警告ログに出す名前
        /// @param requireSceneColor SceneColor が無ければ撃たないか（屈折・反射は再投影に使うので必須）
        /// @return 前提が揃ったか。false のときは撃ってはいけない（理由は警告ログ済み）
        static bool BuildFrameInput(
            const RenderContext& context,
            const WaterSurfaceData& surfaceData,
            const char* debugLabel,
            bool requireSceneColor,
            FrameInput& out);

    protected:
        /// @brief パイプライン 1 本分の構成（差分はこれだけ。残りは全パス同型）
        /// @warning 文字列・配列とも静的記憶域のものを渡すこと。
        ///          initializer_list だと一時配列がその行で寿命を終えてダングリングになる。
        struct RTWaterPipelineDesc {
            const char* ownerName = nullptr;         ///< ログ・診断に出す所有者名
            const char* outputDebugName = nullptr;   ///< 出力テクスチャのデバッグ名接頭辞
            const wchar_t* shaderPath = nullptr;     ///< DXR ライブラリ（lib_6_6）のパス
            const wchar_t* rayGenName = nullptr;     ///< レイ生成シェーダー（レイは中から RayQuery で撃つ）
            const char* outputUavName = nullptr;     ///< u0 の UAV テーブル名
            std::span<const char* const> srvTableNames; ///< t1〜 の SRV テーブル名（t0 は常に gScene・静的配列を渡すこと）
            const char* constantsName = nullptr;     ///< 32bit root constants の名前
            uint32_t constantsBytes = 0;             ///< root constants のサイズ（4 の倍数）
            const char* secondaryOutputUavName = nullptr; ///< u1 の UAV テーブル名（2 枚目の出力を持つパスだけ）
            const char* extraConstantBufferName = nullptr; ///< 追加の CBV 名（持つパスだけ。ディスパッチ時にアドレスを渡す）
            bool directlyIndexedHeap = false;         ///< シェーダーが ResourceDescriptorHeap でヒープを直接引く
            const char* rayRecordLabel = nullptr;     ///< 画素デバッガの表に出す名前（画面の画素ごとに撃つパスだけ）
        };

        /// @brief 2 枚目の出力テクスチャ（BindAndDispatchRays へ渡す）
        struct RTWaterSecondaryOutput {
            GpuResource* resource = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
        };

        /// @brief SRV バインド 1 本分（BindAndDispatchRays へ渡す）
        struct RTWaterSrvBinding {
            const char* name;
            D3D12_GPU_DESCRIPTOR_HANDLE handle;
        };

        /// @brief 構成データからルートシグネチャ・ステートオブジェクト・シェーダーテーブルを構築する
        /// @details 失敗時はエラーログ済み。成功時は isInitialized_ = true。
        bool InitializeFromDesc(const RTEffectInitContext& init, const RTWaterPipelineDesc& desc);

        /// @brief ディスパッチ末尾の共通処理（バインド → DispatchRays → 出力ステート遷移）
        /// @details gScene（TLAS）は内部でバインドする。
        /// @param surfaceConstants UploadSurfaceDataForDispatch が置いた gWaterSurfaceData（b1）の場所
        /// @param constantsBlob InitializeFromDesc で渡した constantsBytes と同サイズの定数ブロック
        /// @param finalState    出力テクスチャの最終ステート（2 枚目の出力も同じステートへ）
        /// @param secondaryOutput 2 枚目の出力（desc.secondaryOutputUavName を持つパスは必須）
        /// @param extraConstantBuffer 追加の CBV のアドレス（desc.extraConstantBufferName を持つパスは必須）
        void BindAndDispatchRays(
            ID3D12GraphicsCommandList* cmdList,
            DispatchResources& resources,
            D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants,
            std::initializer_list<RTWaterSrvBinding> srvBindings,
            const void* constantsBlob,
            UINT width,
            UINT height,
            D3D12_RESOURCE_STATES finalState,
            const RTWaterSecondaryOutput& secondaryOutput = {},
            D3D12_GPU_VIRTUAL_ADDRESS extraConstantBuffer = 0);

        /// @brief ディスパッチ前の共通処理（RayTracingPassBase::BeginDispatchBase の水面版）
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

        /// @brief b1 の水面サーフェス定数を記録中のフレームの UploadRing に置いた結果
        struct SurfaceConstantsUpload {
            WaterSurfaceConstants constants{};     ///< 置いた内容
            D3D12_GPU_VIRTUAL_ADDRESS address = 0; ///< BindAndDispatchRays へ渡す場所（そのフレームの記録中だけ有効）
        };

        /// @brief b1 の水面サーフェス定数を構築し、記録中のフレームの UploadRing に置く
        /// @param fftOceanInput FFT 有効フラグ/解像度を b1 へ載せる
        SurfaceConstantsUpload UploadSurfaceDataForDispatch(
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput) const;

    private:
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
        size_t slotSecondaryOutputUav_ = 0;
        size_t slotExtraConstantBuffer_ = 0;
        bool hasSecondaryOutput_ = false;
        bool hasExtraConstantBuffer_ = false;
        uint32_t constantsBytes_ = 0;

        float lastWaterHeight_ = 0.0f;
        uint32_t lastActiveWaveCount_ = 0;
    };
}
