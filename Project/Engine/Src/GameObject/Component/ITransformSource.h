#pragma once

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
/// @brief 編集可能な位置・回転・スケールを公開するコンポーネントの共通インターフェース
///
/// @details ギズモ・インスペクタ・Undo/Redo は「このオブジェクトの translate/rotate/scale を
///          直接いじりたい」だけであって、それが `WorldTransform`（3D モデル用。GPU 定数
///          バッファつき）なのか `EulerTransform`（スプライト・パーティクル用の素の 3 値）
///          なのかを知る必要はない。その差を吸収するのがこのインターフェース。
///
///          以前は `Editor/ImGui/GameObjectDebugAccess.h` が
///          @code
///          if (auto* s = dynamic_cast<SpriteObject*>(obj))     { ...SpriteTransform...  }
///          if (auto* m = dynamic_cast<ModelGameObject*>(obj))  { ...WorldTransform...   }
///          return false;   // UIImage と ParticleSystem はここで落ちる＝ギズモが効かない
///          @endcode
///          という具象型のダウンキャストで吸収していた。
///
///          **`ComponentHost::GetComponent<T>()` は `dynamic_cast` で解決するので、
///          この基底インターフェースを指定して引ける**。
///          @code
///          if (auto* src = obj->GetComponent<ITransformSource>()) {
///              src->Translate().x += 1.0f;   // 実体が何であれ動く
///          }
///          @endcode
///
///          **座標系はコンポーネントごとに異なる**点に注意。3D モデルはワールド空間、
///          UI・スプライトはスクリーン空間。混ぜて計算してはいけない。
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
