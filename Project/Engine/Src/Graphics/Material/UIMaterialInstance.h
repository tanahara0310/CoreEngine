#pragma once

#include "SpriteMaterialInstance.h"

namespace CoreEngine
{
    /// @brief UI 専用マテリアル（現状は SpriteMaterialInstance と同等）
    /// @details
    ///  - GPU 定数バッファレイアウトはスプライトと完全に一致するため継承で再利用する
    ///  - 将来 UI 固有データ（cornerRadius・gradient 等）が必要になったタイミングで
    ///    独自の `UIMaterialData` 構造体に切り替える前提
    class UIMaterialInstance : public SpriteMaterialInstance {
    public:
        // 現状はスプライトと同一のため追加実装なし
        // 将来 UI 専用パラメータをここに追加する
    };

} // namespace CoreEngine
