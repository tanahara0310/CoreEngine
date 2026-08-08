#pragma once

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
/// @brief 編集可能な translate/rotate/scale を公開する共通インターフェース。
/// @details ギズモ・インスペクタ・Undo/Redo が実体の型（WorldTransform / EulerTransform）を
///          知らずに `GetComponent<ITransformSource>()` で引くための口。座標系は実体ごとに違う。
class ITransformSource {
public:
    virtual ~ITransformSource() = default;

    /// @brief 位置への参照（直接書き換え可能）
    virtual Vector3& Translate() = 0;

    /// @brief 回転への参照（ラジアン。直接書き換え可能）
    virtual Vector3& Rotate() = 0;

    /// @brief スケールへの参照（直接書き換え可能）
    virtual Vector3& Scale() = 0;
};
}
