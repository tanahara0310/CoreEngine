#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/ModelData.h"

namespace CoreEngine
{
    /// @brief モデルのファイル 1 つから取り込んだ物（.cmesh の中身）
    struct MeshAsset
    {
        bool hasMesh = false;        ///< メッシュを持つか（アニメーションだけのファイルは false）
        ModelData model;             ///< LOD の生成と GPU 向けの最適化を済ませたメッシュ
        uint32_t lod0IndexCount = 0; ///< 元の（LOD0 の）インデックス数。LOD1・LOD2 の分はこの後ろに続く
        std::vector<std::pair<std::string, Animation>> animations; ///< ファイル内のアニメーション（ファイル内の順）
    };
}
