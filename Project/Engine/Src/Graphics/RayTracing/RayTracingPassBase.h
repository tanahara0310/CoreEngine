#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>

#include "Graphics/RayTracing/GlobalRootSignatureManager.h"
#include "Graphics/RayTracing/RayTracingDispatchInfo.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "Graphics/RayTracing/ShaderTableBuilder.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief DXR パス（シャドウ・水面屈折/反射/コースティクス等）の共通基盤
    /// @details 出力テクスチャ管理・ディスパッチ前のガード判定・状態遷移・診断情報を集約する。
    ///          派生側に残るのはシェーダー・ルートシグネチャ・固有の定数バッファだけ。
    class RayTracingPassBase {
    public:
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 直近のディスパッチ診断情報（全 RT パス共通の型）
        const RayTracingDispatchInfo& GetDispatchInfo() const { return lastDispatchInfo_; }

    protected:
        /// @brief ディスパッチ前ガードの判定結果
        enum class DispatchGuardStatus : uint32_t {
            Ok = 0,
            NotInitialized,
            RayTracingUnsupported,
            NoBLAS,
            InvalidCommandList,
            CommandList4Unavailable,
            OutputAllocationFailed,
        };

        /// @brief BeginDispatch が用意する、ディスパッチ 1 回分のリソース一式
        struct DispatchResources {
            D3D12_GPU_DESCRIPTOR_HANDLE outputSrvHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle{};
            GpuResource* output = nullptr;   ///< 出力テクスチャ（実体＋現在ステート）
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        };

        /// @param ownerName        ログ・診断に出す所有者名（以後 ownerName_ として使い回す）
        /// @param outputDebugName  出力テクスチャのデバッグ名の接頭辞
        bool InitializeBase(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr,
            const char* ownerName,
            const char* outputDebugName);

        /// @brief ディスパッチ前の共通処理（ガード判定 → 出力・定数バッファの確保 → CommandList4 取得）
        /// @param constantBufferSize 0 以外なら、そのサイズのアップロード定数バッファを確保する
        /// @note 失敗時は lastDispatchInfo_ と警告ログまで済ませるので、false なら即 return してよい
        bool BeginDispatchBase(
            ID3D12GraphicsCommandList* cmdList,
            UINT width,
            UINT height,
            uint32_t viewIndex,
            DispatchResources& outResources,
            DXGI_FORMAT format,
            UINT constantBufferSize);

        /// @brief 診断情報の共通項目を初期化する（ディスパッチの冒頭で呼ぶ）
        void BeginDiagnosticsBase(uint32_t viewIndex, UINT width, UINT height);

        /// @brief アップロードヒープの定数バッファを確保しマップする（確保済みなら何もしない）
        bool EnsureConstantBuffer(UINT bufferSize);

        void ReleaseOutputIfSizeMismatchBase(UINT width, UINT height, uint32_t viewIndex);

        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRVHandleBase(uint32_t viewIndex) const;
        /// @brief 出力テクスチャをステート追跡つきで返す（バリア発行はこれを渡す）
        GpuResource& GetOutputBase(uint32_t viewIndex);

        void BeginOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& output) const;
        void EndOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& output,
            D3D12_RESOURCE_STATES finalState) const;

        const char* GetOwnerName() const { return ownerName_; }

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        uint8_t* constantBufferMapped_ = nullptr;
        GlobalRootSignatureManager globalRootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;
        ShaderTableBuilder shaderTableBuilder_;
        RayTracingOutputViewSet outputViews_;

        RayTracingDispatchInfo lastDispatchInfo_{};
        bool isInitialized_ = false;

    private:
        bool EnsureOutputTextureBase(UINT width, UINT height, uint32_t viewIndex, DXGI_FORMAT format);
        DispatchGuardStatus ValidateDispatchPreconditions(ID3D12GraphicsCommandList* cmdList) const;
        bool QueryCommandList4(
            ID3D12GraphicsCommandList* cmdList,
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4) const;
        /// @brief ガード失敗を診断情報へ反映し、警告ログを出す
        void ReportDispatchGuardFailure(DispatchGuardStatus guardStatus, ID3D12GraphicsCommandList* cmdList);

        const char* ownerName_ = "RayTracingPassBase";
        const char* outputDebugName_ = "RTOutput";
    };
}
