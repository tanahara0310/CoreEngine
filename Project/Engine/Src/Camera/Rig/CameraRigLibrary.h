#pragma once

#include "Camera/Rig/CameraRigTypes.h"
#include "Utility/Asset/NamedAssetLibrary.h"

/// @file
/// @brief リグ名からアセットを引くためのキャッシュ付きロード

namespace CoreEngine
{
    /// @brief 名前 → リグの対応を持つ読み込みキャッシュ
    /// @note 中身は NamedAssetLibrary（読む関数と一覧の関数だけを渡す）。
    class CameraRigLibrary final : public NamedAssetLibrary<CameraRigAsset>
    {
    public:
        CameraRigLibrary();
    };
}
