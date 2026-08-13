#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class RenderTargetManager;

    /// @brief ポストエフェクトがフレーム内で使う一時レンダーターゲットのプール
    /// @details エフェクトが CreateTransient を呼ぶたびにスロットを 1 つ払い出す。
    ///          **同一フレーム内で払い出したスロットは使い回さない**（別用途で上書きされるため）。
    ///          再利用はフレームをまたいだ場合だけで、確保済みターゲットは破棄しない。
    ///          エフェクト構成が毎フレーム同じである限り、要求列も同じ順序になるので
    ///          スロットの寸法・フォーマットは安定し、作り直しは起きない。
    ///          設計: Docs/Engine/Graphics/PostProcess/PostEffect_Refactoring_Plan.md
    class PostEffectTransientPool {
    public:
        /// @brief フレーム開始時に払い出しカーソルを戻す
        void BeginFrame()
        {
            cursor_ = 0;
            acquiredThisFrame_.clear();
        }

        /// @brief 一時ターゲットを 1 枚払い出す
        /// @param manager 実体を作る RenderTargetManager
        /// @param width   必要な幅[px]
        /// @param height  必要な高さ[px]
        /// @param format  必要なフォーマット
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
