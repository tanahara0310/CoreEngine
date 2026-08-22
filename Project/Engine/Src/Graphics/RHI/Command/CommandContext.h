#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>

#include "Graphics/RHI/Command/FrameSync.h"

namespace CoreEngine
{
    /// @brief フレーム描画用のコマンドアロケータ＋コマンドリスト
    /// @details アロケータはフレームスロットごとに持ち、リストは 1 本を使い回す。
    /// @note 記録はメインスレッド専用。ワーカースレッドからのアップロードは
    ///       UploadContext を使うこと（フレームの記録に割り込むとフレームごと submit される）。
    class CommandContext
    {
    public:
        /// @brief 初期化（framesInFlight 本のアロケータとリスト 1 本を生成する）
        /// @note 生成直後のコマンドリストは「記録中」のまま返る（フレーム 0 をそのまま記録できる）
        void Initialize(ID3D12Device* device, uint32_t framesInFlight);

        void Shutdown();

        ID3D12GraphicsCommandList* List() const { return list_.Get(); }

        /// @brief 記録を閉じる
        /// @return 成功したら true
        bool Close();

        /// @brief 指定スロットのアロケータで記録を開始する
        /// @details アロケータの Reset は「そのスロットの GPU 作業が完了済み」が前提。
        ///          呼び出し前に FrameSync::WaitForFrame() を通すこと。
        /// @param srvHeap フレーム先頭で 1 回だけバインドするシェーダ可視ヒープ（nullptr 可）
        void Begin(uint32_t frameIndex, ID3D12DescriptorHeap* srvHeap);

        /// @brief シェーダ可視ヒープを今のリストへバインドする
        /// @details **フレーム 0 用**。Initialize 直後のコマンドリストは Begin() を通らずに
        ///          そのまま記録されるため、ここで一度バインドしておかないと
        ///          最初のフレームだけディスクリプタヒープ未設定のまま
        ///          SetGraphicsRootDescriptorTable が呼ばれる（D3D12 ERROR）。
        void BindDescriptorHeap(ID3D12DescriptorHeap* srvHeap);

    private:
        std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> allocators_{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
        uint32_t framesInFlight_ = 2;
    };
}
