#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief 2 つの値を「書く側」と「読む側」に分けて交互に使う
    /// @details 読む側は書く側でない方（前に書いた側）。Swap で入れ替えるか、
    ///          番号の偶奇で書く側を選ぶ（SelectByParity）。
    template <class T>
    class PingPong
    {
    public:
        /// @brief 持つ値の数
        static constexpr std::size_t kCount = 2;

        /// @brief 書く側
        T& Write() noexcept { return items_[writeIndex_]; }
        const T& Write() const noexcept { return items_[writeIndex_]; }

        /// @brief 読む側（書く側でない方）
        T& Read() noexcept { return items_[ReadIndex()]; }
        const T& Read() const noexcept { return items_[ReadIndex()]; }

        /// @brief 書く側の番号（0 か 1）
        uint32_t WriteIndex() const noexcept { return writeIndex_; }

        /// @brief 読む側の番号（0 か 1）
        uint32_t ReadIndex() const noexcept { return writeIndex_ ^ 1u; }

        /// @brief 書く側と読む側を入れ替える
        void Swap() noexcept { writeIndex_ ^= 1u; }

        /// @brief number の偶奇で書く側を選ぶ（偶数 = 0・奇数 = 1）
        void SelectByParity(uint64_t number) noexcept { writeIndex_ = static_cast<uint32_t>(number & 1u); }

        /// @brief index（0 か 1）の側を読む側にする（書く側はもう片方）
        void SetReadIndex(uint32_t index) noexcept { writeIndex_ = (index & 1u) ^ 1u; }

        /// @brief 番号で値を引く（書く側・読む側によらない）
        T& operator[](std::size_t index) noexcept { return items_[index]; }
        const T& operator[](std::size_t index) const noexcept { return items_[index]; }

        static constexpr std::size_t size() noexcept { return kCount; }

        auto begin() noexcept { return items_.begin(); }
        auto end() noexcept { return items_.end(); }
        auto begin() const noexcept { return items_.begin(); }
        auto end() const noexcept { return items_.end(); }

    private:
        std::array<T, kCount> items_{};
        uint32_t writeIndex_ = 0;
    };
}
