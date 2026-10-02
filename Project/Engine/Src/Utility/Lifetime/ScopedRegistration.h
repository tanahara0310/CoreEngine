#pragma once

#include <functional>
#include <utility>

namespace CoreEngine
{
    /// @brief 登録を握るハンドル。破棄すると登録が外れる
    /// @details ムーブだけできる。登録する関数は、登録を外す処理をこの型に包んで返す。
    class [[nodiscard]] ScopedRegistration
    {
    public:
        ScopedRegistration() = default;

        /// @param unregister 登録を外す処理（破棄か Reset のときに 1 回だけ呼ばれる）
        explicit ScopedRegistration(std::function<void()> unregister)
            : unregister_(std::move(unregister)) {}

        ~ScopedRegistration() { Reset(); }

        ScopedRegistration(const ScopedRegistration&) = delete;
        ScopedRegistration& operator=(const ScopedRegistration&) = delete;

        ScopedRegistration(ScopedRegistration&& other) noexcept
            : unregister_(std::exchange(other.unregister_, nullptr)) {}

        ScopedRegistration& operator=(ScopedRegistration&& other) noexcept
        {
            if (this != &other) {
                Reset();
                unregister_ = std::exchange(other.unregister_, nullptr);
            }
            return *this;
        }

        /// @brief 登録を外す（外した後は何もしない）
        void Reset()
        {
            if (std::function<void()> unregister = std::exchange(unregister_, nullptr)) {
                unregister();
            }
        }

        /// @brief 登録を握っているか
        explicit operator bool() const noexcept { return static_cast<bool>(unregister_); }

    private:
        std::function<void()> unregister_;
    };
}
