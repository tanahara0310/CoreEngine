#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief ウィンドウリサイズ通知を受け取るクラスが実装するインターフェース
    /// @details GraphicsCore::RegisterResizable() で登録すると、
    ///          メインスワップチェーンの再作成が完了した後、登録順に
    ///          GraphicsCore::OnWindowResize() から呼ばれる。
    ///          登録した側は破棄時に必ず GraphicsCore::UnregisterResizable() で解除すること
    ///          （解除 API が無かった頃は「GraphicsCore より先に壊すな」という順序依存だった）。
    class IResizable {
    public:
        virtual ~IResizable() = default;

        /// @brief ウィンドウリサイズ時の処理
        /// @param width 新しいクライアント領域の幅
        /// @param height 新しいクライアント領域の高さ
        virtual void OnWindowResize(int32_t width, int32_t height) = 0;
    };
}
