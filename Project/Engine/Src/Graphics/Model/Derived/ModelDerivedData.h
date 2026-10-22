#pragma once

#include <filesystem>

#include "Graphics/Asset/Derived/DerivedData.h"
#include "Graphics/Model/Import/MeshAsset.h"

namespace CoreEngine
{
    /// @brief モデルのファイルの派生データ（.cmesh）を探す・作る・読む
    /// @details クック済みは `Application/Cooked/a/m.gltf.cmesh`、
    ///          開発中は `<プロジェクト>/Intermediate/DerivedData/Mesh/<GUID>.cmesh`（GUID が無ければ綴りのハッシュ）。
    namespace ModelDerivedData
    {
        /// @brief 元のファイルの綴りから、クック済みの .cmesh の綴りを作る（Assets の下でなければ空）
        std::filesystem::path ToCookedPath(const std::filesystem::path& sourceRelative);

        /// @brief 元のファイルに対応する .cmesh を探す（クック済み → 開発中の置き場 → いま変換）
        DerivedData::Result Find(const std::filesystem::path& source);

        /// @brief 開発中の置き場の .cmesh を新しくして返す（書き出しのクックが使う）
        DerivedData::Result EnsureIntermediate(const std::filesystem::path& source);

        /// @brief 元のファイルの .cmesh を探して読む
        /// @return 読めたら true
        bool Load(const std::filesystem::path& source, MeshAsset& asset);
    }
}
