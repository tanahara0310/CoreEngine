#pragma once

#include "Graphics/RHI/Resource/UploadRing.h"

#include <d3d12.h>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief 毎フレーム内容が変わる定数を 1 つ持ち、GPU へは記録中のフレームの UploadRing から渡す型

namespace CoreEngine
{
    /// @brief 毎フレーム内容が変わる定数を 1 つ持ち、GPU へは記録中のフレームの UploadRing から渡す
    /// @details 値は CPU 側に持つ。Address() を呼んだフレームで初めて UploadRing に置き、
    ///          同じフレームで値を変えずに呼べば同じ場所を返す。値を変えた後とフレームが進んだ後は置き直す。
    ///          書く口は Set() だけで、常時 Map した 1 枠を上書きする書き方はできない。
    /// @warning Address() はコマンド記録スレッドからのみ呼ぶ（UploadRing と同じ制約）
    template <class T>
    class PerFrameConstants {
        static_assert(std::is_trivially_copyable_v<T>, "PerFrameConstants: 定数は trivially copyable であること");

    public:
        /// @brief 置き場所の UploadRing をつなぐ
        void Initialize(UploadRing& ring)
        {
            ring_ = &ring;
            uploadedGeneration_ = 0;
        }

        /// @brief UploadRing につながっているか
        bool IsReady() const noexcept { return ring_ != nullptr; }

        /// @brief 値を差し替える（GPU へは次の Address() で置く）
        void Set(const T& value)
        {
            value_ = value;
            uploadedGeneration_ = 0;
        }

        /// @brief 今の値
        const T& Get() const noexcept { return value_; }

        /// @brief 今の値を記録中のフレームの UploadRing に置き、ルートへ渡す GPU アドレスを返す（失敗時は 0）
        /// @note 返したアドレスはそのフレームの記録中だけ有効。フレームをまたいで持ち越さないこと
        D3D12_GPU_VIRTUAL_ADDRESS Address() const
        {
            if (!ring_) {
                return 0;
            }
            const uint64_t generation = ring_->Generation();
            if (uploadedGeneration_ != generation) {
                address_ = ring_->AllocateConstants(value_);
                uploadedGeneration_ = (address_ != 0) ? generation : 0;
            }
            return address_;
        }

    private:
        T value_{};
        UploadRing* ring_ = nullptr;
        mutable D3D12_GPU_VIRTUAL_ADDRESS address_ = 0;
        /// @brief address_ を置いたときの UploadRing の世代（0 はまだ置いていない）
        mutable uint64_t uploadedGeneration_ = 0;
    };
}
