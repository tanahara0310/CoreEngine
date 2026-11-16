#pragma once

#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>

/// @file
/// @brief GPU が書いた値を CPU へ読み戻すバッファを、フレームの枠の数だけ持つ型

namespace CoreEngine
{
    /// @brief フレームの枠ごとの読み戻し用バッファと、枠ごとの記録
    /// @details 枠は記録してから、読み終えるか捨てるまで「待ち」になる。
    ///          GPU がその枠を終えたかは、フェンスの値で確かめる（TryTakeCompleted）か、
    ///          同じ枠が回ってきたことで済んだとみなす（TakePending）。
    /// @tparam TMeta 枠ごとの記録の型（何を写したか・読んだ値をどう使うか）
    template <class TMeta>
    class ReadbackRing
    {
    public:
        /// @brief 枠の数だけ READBACK ヒープのバッファを作る
        /// @param bytes バッファ 1 つのバイト数
        /// @return 全部作れたら true
        bool Create(const Microsoft::WRL::ComPtr<ID3D12Device>& device, size_t bytes)
        {
            try {
                for (auto& buffer : buffers_) {
                    buffer = ResourceFactory::CreateBufferResource(device, bytes, D3D12_HEAP_TYPE_READBACK);
                }
            }
            catch (const std::exception&) {
                return false;
            }
            return true;
        }

        /// @brief 枠の読み戻し用バッファ（GPU からの写し先）
        ID3D12Resource* Buffer(uint32_t slot) const { return buffers_[slot].Get(); }

        /// @brief 枠の記録
        TMeta& Meta(uint32_t slot) { return slots_[slot].meta; }
        const TMeta& Meta(uint32_t slot) const { return slots_[slot].meta; }

        /// @brief 枠が読み戻し待ちか
        bool IsPending(uint32_t slot) const { return slots_[slot].pending; }

        /// @brief 枠を待ちにし、今の枠のフェンス値を覚える（この値から変われば、そのフレームを GPU へ投げた）
        void MarkSubmitted(uint32_t slot, const FrameSync& sync)
        {
            slots_[slot].pending = true;
            slots_[slot].fenceBeforeSubmit = sync.FenceValueOf(slot);
        }

        /// @brief 枠を待ちにする（フェンス値は覚えない。TakePending で読む枠に使う）
        void MarkPending(uint32_t slot) { slots_[slot].pending = true; }

        /// @brief 枠の待ちを捨てる（記録は残す）
        void Discard(uint32_t slot) { slots_[slot].pending = false; }

        /// @brief 全部の枠の待ちを捨てる（記録は残す）
        void DiscardAll()
        {
            for (Slot& slot : slots_) {
                slot.pending = false;
            }
        }

        /// @brief 枠の待ち・フェンス値・記録を初めの値に戻す
        void Reset(uint32_t slot) { slots_[slot] = Slot{}; }

        /// @brief 待ちの枠を GPU が終えていれば待ちを外す
        /// @details 覚えたフェンス値のままの枠（まだ GPU へ投げていない）と、フェンスが届いていない枠は待ちのまま残す
        /// @return 読んでよければ true
        bool TryTakeCompleted(uint32_t slot, const FrameSync& sync)
        {
            Slot& state = slots_[slot];
            if (!state.pending) {
                return false;
            }
            const uint64_t fence = sync.FenceValueOf(slot);
            if (fence == state.fenceBeforeSubmit || sync.CompletedValue() < fence) {
                return false;
            }
            state.pending = false;
            return true;
        }

        /// @brief 待ちの枠の待ちを外す（GPU がその枠を終えたことは呼び出し側が保証する）
        /// @return 待ちだったら true
        bool TakePending(uint32_t slot)
        {
            if (!slots_[slot].pending) {
                return false;
            }
            slots_[slot].pending = false;
            return true;
        }

        /// @brief 枠のバッファの先頭 bytes を Map し、read(const void* mapped) に渡してから Unmap する
        /// @return Map できたら true
        template <class TRead>
        bool Read(uint32_t slot, size_t bytes, TRead&& read) const
        {
            ID3D12Resource* buffer = buffers_[slot].Get();
            void* mapped = nullptr;
            const D3D12_RANGE readRange{ 0, bytes };
            if (FAILED(buffer->Map(0, &readRange, &mapped)) || !mapped) {
                return false;
            }
            read(static_cast<const void*>(mapped));
            const D3D12_RANGE writtenRange{ 0, 0 };
            buffer->Unmap(0, &writtenRange);
            return true;
        }

        /// @brief 古い枠から順に func(slot) を呼ぶ（今の枠が最後）
        template <class TFunc>
        void ForEachOldestFirst(const FrameSync& sync, TFunc&& func) const
        {
            const uint32_t framesInFlight = sync.FramesInFlight();
            const uint32_t current = sync.FrameIndex();
            for (uint32_t offset = 1; offset <= framesInFlight; ++offset) {
                func((current + offset) % framesInFlight);
            }
        }

    private:
        /// @brief 枠 1 つ分の状態
        struct Slot {
            bool pending = false;
            /// @brief MarkSubmitted で覚えた枠のフェンス値
            uint64_t fenceBeforeSubmit = 0;
            TMeta meta{};
        };

        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> buffers_{};
        std::array<Slot, kMaxFramesInFlight> slots_{};
    };
}
