#pragma once

#include "Camera/Sequence/CameraSequenceTypes.h"
#include "Utility/Asset/NamedAssetLibrary.h"

/// @file
/// @brief シーケンス名からアセットを引くためのキャッシュ付きロード

namespace CoreEngine
{
    /// @brief 名前 → シーケンスの対応を持つ読み込みキャッシュ
    /// @note 中身は NamedAssetLibrary（読む関数と一覧の関数だけを渡す）。
    class CameraSequenceLibrary final : public NamedAssetLibrary<CameraSequenceAsset>
    {
    public:
        CameraSequenceLibrary();
    };
}
