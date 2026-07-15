#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief ウィンドウリサイズ通知を受け取るクラスが実装するインターフェース
    /// @details DirectXCommon::RegisterResizable() で登録すると、
    ///          スワップチェーン/深度バッファの再作成が完了した後、
    ///          DirectXCommon::OnWindowResize() から呼ばれる。
    class IResizable {
    public:
        virtual ~IResizable() = default;

        /// @brief ウィンドウリサイズ時の処理
        /// @param width 新しいクライアント領域の幅
        /// @param height 新しいクライアント領域の高さ
        virtual void OnWindowResize(int32_t width, int32_t height) = 0;
    };
}
