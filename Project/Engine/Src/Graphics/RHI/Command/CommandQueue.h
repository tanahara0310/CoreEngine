#pragma once

#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief 描画用コマンドキュー
    /// @details 旧 CommandManager からキューの責務だけを取り出したもの。
    ///          フェンス／フレーム番号は FrameSync、アロケータ／リストは CommandContext が持つ。
    class CommandQueue
    {
    public:
        void Initialize(ID3D12Device* device);
        void Shutdown();

        ID3D12CommandQueue* Get() const { return queue_.Get(); }

        /// @brief コマンドリストを 1 本投入する
        void Execute(ID3D12GraphicsCommandList* list);

    private:
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    };
}
