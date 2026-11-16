#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace CoreEngine
{
    /// @brief 決まった数の値を輪で持つ履歴（いっぱいになると最も古い値へ上書きする）
    template <class T, std::size_t N>
    class RingHistory
    {
        static_assert(N > 0);

    public:
        /// @brief 持てる値の数
        static constexpr int kCapacity = static_cast<int>(N);

        /// @brief 値を 1 つ足す
        void Push(const T& value)
        {
            values_[static_cast<std::size_t>(head_)] = value;
            head_ = (head_ + 1) % kCapacity;
            count_ = (std::min)(count_ + 1, kCapacity);
        }

        /// @brief 古い順に i 番目の値
        const T& At(int i) const
        {
            const int oldest = (head_ - count_ + kCapacity) % kCapacity;
            return values_[static_cast<std::size_t>((oldest + i) % kCapacity)];
        }

        /// @brief 全部の値を value で埋める（書く位置と数はそのまま）
        void Fill(const T& value) { values_.fill(value); }

        /// @brief 置き場の先頭（書いた順に輪で並ぶ。ImGui::PlotLines へは Head() を始まりとして渡す）
        const T* Data() const noexcept { return values_.data(); }

        /// @brief 次に書く位置（いっぱいのときは最も古い値の位置）
        int Head() const noexcept { return head_; }

        /// @brief 入っている値の数
        int Count() const noexcept { return count_; }

    private:
        std::array<T, N> values_{};
        int head_ = 0;
        int count_ = 0;
    };
}
