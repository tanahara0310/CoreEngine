#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <unordered_map>
#include <vector>

#include "InstanceBatch.h"
#include "Graphics/Model/TransformationMatrix.h"

namespace CoreEngine
{
    class DirectXCommon;
    class BaseModelRenderer;
    struct ModelDrawPacket;

    /// @brief モデルのインスタンシング描画を一括管理するマネージャー
    /// アプリケーションからは Submit() で行列を登録するだけ。
    /// パス終了時に Flush() がバッチごとに DrawIndexedInstanced を発行する。
    class InstanceBatchManager {
    public:
        /// @brief 初期化（フレーム数分の Upload Heap を確保）
        /// @param dxCommon DirectXCommon
        /// @param frameCount スワップチェーンのバックバッファ数
        /// @param maxInstancesPerFrame 1 フレームあたりの最大インスタンス数
        void Initialize(DirectXCommon* dxCommon, uint32_t frameCount,
            uint32_t maxInstancesPerFrame = 8192);

        /// @brief 描画リクエストを登録（即時描画はせずバッチに積む）
        void Submit(const InstanceBatchKey& key, const TransformationMatrix& matrix,
            D3D12_GPU_VIRTUAL_ADDRESS materialCBV);

        /// @brief パス終了時に呼び出し、バッチを GPU にアップロードして描画する
        /// @param cmdList コマンドリスト
        /// @param renderer 現在のレンダラー（ルートパラメータ解決に使用）
        /// @param isGBufferPass 現在のパスが GBuffer か（パスマッチング用）
        void Flush(ID3D12GraphicsCommandList* cmdList, BaseModelRenderer* renderer,
            bool isGBufferPass);

    private:
        /// @brief バッチ 1 つを描画する
        void DrawBatch(ID3D12GraphicsCommandList* cmdList, BaseModelRenderer* renderer,
            const InstanceBatch& batch);

        /// @brief Upload Heap に行列をコピーし、書き込み先頭の GPU 仮想アドレスを返す
        /// @return Root SRV にバインドする GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS UploadInstanceData(
            const std::vector<TransformationMatrix>& instances);

    private:
        DirectXCommon* dxCommon_ = nullptr;

        // フレームごとの Upload Heap（CPU 書き込み用）
        struct FrameResource {
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
            TransformationMatrix*                  mapped = nullptr;
            uint32_t                               offset = 0; ///< 次に書き込むインスタンス位置
        };
        std::vector<FrameResource> frameResources_;
        uint32_t maxInstancesPerFrame_ = 0;
        uint32_t currentFrameIndex_ = 0;

        // 現フレームのバッチ集合（Flush でクリア）
        std::unordered_map<InstanceBatchKey, InstanceBatch, InstanceBatchKeyHash> batches_;

        /// @brief 現在のフレームインデックスを取得して、必要なら次フレームに切り替える
        void UpdateFrameIndex();
    };
}
