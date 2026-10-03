#pragma once

#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"

#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

/// @file
/// @brief 毎フレーム内容が変わる構造化バッファを、フレームスロットごとの UPLOAD バッファと SRV で持つ型

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief PerFrameStructuredBuffer の要素の型によらない部分（フレームスロットごとの UPLOAD バッファと SRV）
    class PerFrameBufferSlots
    {
    public:
        PerFrameBufferSlots() = default;

        /// @brief バッファと SRV を、GPU が使い終えてから返すよう預ける
        ~PerFrameBufferSlots();

        PerFrameBufferSlots(PerFrameBufferSlots&& other) noexcept;
        PerFrameBufferSlots& operator=(PerFrameBufferSlots&& other) noexcept;
        PerFrameBufferSlots(const PerFrameBufferSlots&) = delete;
        PerFrameBufferSlots& operator=(const PerFrameBufferSlots&) = delete;

        /// @brief フレームスロットの数だけ UPLOAD バッファと構造化バッファの SRV を作る
        /// @param stride 要素 1 つのバイト数
        /// @param capacity 要素の最大数
        void Initialize(GraphicsCore& graphics, uint32_t stride, uint32_t capacity, const char* debugName);

        /// @brief バッファと SRV を、GPU が使い終えてから返すよう預ける
        void Release();

        /// @brief 次の Upload で写し直すようにする
        void Invalidate() noexcept { uploadedGeneration_ = 0; }

        /// @brief 記録中のフレームのスロットへまだ写していなければ写し、そのスロットの SRV を返す
        /// @param data 写す値
        /// @param bytes 写すバイト数（容量まで）
        /// @return 作っていなければ ptr が 0
        D3D12_GPU_DESCRIPTOR_HANDLE Upload(const void* data, size_t bytes) const;

    private:
        struct Slot {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            uint8_t* mapped = nullptr;
            UniqueDescriptor srv;
        };

        GraphicsCore* graphics_ = nullptr;
        std::array<Slot, kMaxFramesInFlight> slots_{};
        size_t capacityBytes_ = 0;
        /// @brief 最後に写したときの UploadRing の世代（0 はまだ写していない）
        mutable uint64_t uploadedGeneration_ = 0;
    };

    /// @brief 毎フレーム内容が変わる構造化バッファ（StructuredBuffer<T>）
    /// @details 値は CPU 側に持つ。Srv() を呼んだフレームで初めて、そのフレームのスロット（UPLOAD バッファ）へ写す。
    ///          同じフレームで値を変えずに呼べば写し直さない。スロットは記録中のフレームの番号で選ぶので、
    ///          GPU がまだ読んでいる前のフレームのスロットへは書かない。
    /// @warning Srv() はコマンド記録スレッドからのみ呼ぶ。同じフレームで Srv() の後に値を変えると、
    ///          そのフレームで先に記録した描画も新しい値を読む
    template <class T>
    class PerFrameStructuredBuffer
    {
        static_assert(std::is_trivially_copyable_v<T>, "PerFrameStructuredBuffer: 要素は trivially copyable であること");

    public:
        /// @brief フレームスロットの数だけ UPLOAD バッファと SRV を作る
        /// @param capacity 要素の最大数
        void Initialize(GraphicsCore& graphics, uint32_t capacity, const char* debugName)
        {
            values_.assign(capacity, T{});
            count_ = 0;
            slots_.Initialize(graphics, static_cast<uint32_t>(sizeof(T)), capacity, debugName);
        }

        /// @brief バッファと SRV を、GPU が使い終えてから返すよう預ける
        void Release() { slots_.Release(); }

        /// @brief 要素の最大数
        uint32_t Capacity() const noexcept { return static_cast<uint32_t>(values_.size()); }

        /// @brief 持っている要素の数
        uint32_t Count() const noexcept { return count_; }

        /// @brief 値を差し替える（GPU へは次の Srv() で写す）。最大数を超えた分は捨てる
        /// @return 持った数
        uint32_t Set(std::span<const T> values)
        {
            count_ = static_cast<uint32_t>((std::min)(values.size(), values_.size()));
            std::copy_n(values.begin(), count_, values_.begin());
            slots_.Invalidate();
            return count_;
        }

        /// @brief 値を書き込む先を count 個ぶん返す（最大数まで。GPU へは次の Srv() で写す）
        std::span<T> Write(uint32_t count)
        {
            count_ = (std::min)(count, Capacity());
            slots_.Invalidate();
            return { values_.data(), count_ };
        }

        /// @brief 持つ数を決め直す（Write で書いた先頭の count 個だけを GPU へ写す）
        void SetCount(uint32_t count)
        {
            count_ = (std::min)(count, Capacity());
            slots_.Invalidate();
        }

        /// @brief 今の値を記録中のフレームのスロットへ写し、その SRV を返す（作っていなければ ptr が 0）
        /// @note 返した SRV が指す中身は、そのフレームの記録中だけ今の値を保つ
        D3D12_GPU_DESCRIPTOR_HANDLE Srv() const
        {
            return slots_.Upload(values_.data(), sizeof(T) * count_);
        }

    private:
        std::vector<T> values_;
        uint32_t count_ = 0;
        PerFrameBufferSlots slots_;
    };
}
