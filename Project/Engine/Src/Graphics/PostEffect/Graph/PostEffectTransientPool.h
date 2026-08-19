#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class RenderTargetManager;

    /// @brief ポストエフェクトがフレーム内で使う一時レンダーターゲットのプール
    /// @details CreateTransient のたびにスロットを 1 つ払い出す。
    ///          同一フレーム内で払い出したスロットは使い回さず、再利用はフレームをまたいだ場合だけ。
    class PostEffectTransientPool {
    public:
        /// @brief フレーム開始時に払い出しカーソルを戻す
        void BeginFrame()
        {
            cursor_ = 0;
            acquiredThisFrame_.clear();
        }

        /// @brief 一時ターゲットを 1 枚払い出す
        /// @return 払い出したターゲットの登録名。失敗時は空文字列
        std::string Acquire(RenderTargetManager* manager, uint32_t width, uint32_t height, DXGI_FORMAT format);

        /// @brief 今フレームに払い出した登録名の一覧
        const std::vector<std::string>& AcquiredThisFrame() const { return acquiredThisFrame_; }

    private:
        /// @brief プールの 1 スロット。名前は生成順で固定し、寸法が変わったときだけ作り直す
        struct Slot {
            std::string name;
            uint32_t    width = 0;
            uint32_t    height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        std::vector<Slot>        slots_;
        size_t                   cursor_ = 0;
        std::vector<std::string> acquiredThisFrame_;
    };
}
