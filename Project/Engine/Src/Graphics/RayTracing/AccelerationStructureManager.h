#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <cstdint>
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class DescriptorManager;
    class ModelResource;
    class CommandManager;

    /// @brief BLAS/TLAS を管理するクラス（DXR レイトレーシング用）
    /// @note ID3D12Device5 が必須。非対応 GPU では Initialize() が false を返す
    class AccelerationStructureManager {
    public:
        /// @brief BLAS 構築に必要なメッシュ情報
        struct BLASDesc {
            ID3D12Resource* vertexBuffer = nullptr;
            UINT vertexCount = 0;
            UINT vertexStride = 0;
            DXGI_FORMAT vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            UINT vertexPositionOffset = 0;
            ID3D12Resource* indexBuffer = nullptr;
            UINT indexCount = 0;
            DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
        };

        /// @brief TLAS インスタンス情報
        struct InstanceDesc {
            UINT blasIndex = 0;
            float transform[3][4] = {};   ///< DXR 準拠の行優先 3×4 アフィン行列
            UINT instanceMask = 0xFF;

            /// @brief Matrix4x4 からDXR用 3×4 行列に変換してセットする
            /// @note DXR の D3D12_RAYTRACING_INSTANCE_DESC は行優先 3×4 で
            ///       平行移動が [row][3] に来る形式を要求する。
            ///       エンジンの Matrix4x4 は平行移動が m[3][col] にあるため転置が必要。
            void SetTransform(const Matrix4x4& mat) {
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        transform[row][col] = mat.m[col][row];
                    }
                }
            }
        };

        /// @brief 初期化（DXR サポート確認を含む）
        /// @return DXR 非対応の場合 false
        bool Initialize(ID3D12Device* device, DescriptorManager* descriptorManager);

        /// @brief BLAS を構築して登録する
        /// @return BLAS インデックス（BuildTLAS の InstanceDesc::blasIndex で使う）
        UINT BuildBLAS(ID3D12GraphicsCommandList* cmdList, const BLASDesc& desc);

        /// @brief 全インスタンスから TLAS を構築する（毎フレーム呼び出し可能）
        void BuildTLAS(ID3D12GraphicsCommandList* cmdList,
            const std::vector<InstanceDesc>& instances);

        /// @brief ModelResource から BLAS を構築し、インデックスを ModelResource に設定する
        /// @return 成功した場合 true（既に構築済みの場合も true）
        bool BuildBLASFromModelResource(ID3D12GraphicsCommandList* cmdList, ModelResource* resource);

        /// @brief TLAS の SRV GPU ハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetTLASSRVHandle() const { return tlasSRVHandle_; }

        /// @brief DXR がサポートされているか
        bool IsSupported() const { return isSupported_; }

        /// @brief フレーム終了後に退避リソースを解放する
        /// @param commandManager 現在フレームのGPU完了を待機するために使用する
        /// @param frameIndex SignalFrame で記録したフレームインデックス
        void FlushRetiredResources(CommandManager* commandManager, UINT frameIndex);

        /// @brief 退避リソースがあるか
        bool HasRetiredResources() const { return !retiredResources_.empty(); }

        /// @brief 登録済み BLAS 数を取得
        UINT GetBLASCount() const { return static_cast<UINT>(blasList_.size()); }

    private:
        void EnsureScratchBuffer(UINT64 requiredSize);

        Microsoft::WRL::ComPtr<ID3D12Device5> device5_;
        DescriptorManager* descriptorManager_ = nullptr;

        struct BLASEntry {
            Microsoft::WRL::ComPtr<ID3D12Resource> result;
        };
        std::vector<BLASEntry> blasList_;

        // TLAS リソース
        Microsoft::WRL::ComPtr<ID3D12Resource> tlasResult_;
        Microsoft::WRL::ComPtr<ID3D12Resource> tlasInstanceDescBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> tlasScratch_;
        D3D12_GPU_DESCRIPTOR_HANDLE tlasSRVHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE tlasSRVCpuHandle_{};

        // BLAS 構築用スクラッチ（再利用）
        Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch_;
        UINT64 blasScratchSize_ = 0;

        // コマンドリスト実行完了まで旧リソースを保持する退避リスト
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredResources_;

        bool isSupported_ = false;
    };
}
