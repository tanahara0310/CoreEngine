#pragma once

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
/// @brief 編集できる位置・回転・スケールを値で読み書きする共通インターフェース。
/// @details ギズモ・インスペクタ・Undo/Redo が実体の型（WorldTransform / EulerTransform）を
///          知らずに `GetComponent<ITransformSource>()` で引くための口。座標系は実体ごとに違う。
class ITransformSource {
public:
    virtual ~ITransformSource() = default;

    /// @brief 位置
    virtual Vector3 GetTranslate() const = 0;
    virtual void SetTranslate(const Vector3& translate) = 0;

    /// @brief 回転（オイラー角・ラジアン）
    virtual Vector3 GetRotate() const = 0;
    virtual void SetRotate(const Vector3& radians) = 0;

    /// @brief スケール
    virtual Vector3 GetScale() const = 0;
    virtual void SetScale(const Vector3& scale) = 0;
};
}
