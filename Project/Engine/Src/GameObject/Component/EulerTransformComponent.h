#pragma once

#include "GameObject/Component/IComponent.h"
#include "GameObject/Component/ITransformSource.h"
#include "Math/EulerTransform.h"

namespace CoreEngine
{
/// @brief GPU 定数バッファを持たない軽量トランスフォームコンポーネント
///
/// @details スプライト・UI・パーティクルエミッタ用。これらは
///          `WorldTransform`（3D モデル用）が持つ **per-object の D3D12 定数バッファを
///          必要としない**（自前で WVP を組む／エミッタ位置としてだけ使う）ので、
///          素の `EulerTransform`（scale / rotate / translate の 3 値）を包むだけにしてある。
///
///          **`TransformComponent` と無理に統合しない理由**:
///          - スプライトのスケールはテクスチャ寸法との積で意味が決まる
///          - UI はアンカー相対のスクリーン座標で、そもそも 3D の行列階層ではない
///          - パーティクルはエミッタの発生位置・向き
///          いずれも `WorldTransform` の親子階層＋GPU 転送という意味論とは別物。
///          モデルに合わせて押し込むと「使わない定数バッファを毎フレーム更新する」
///          無駄と「座標系の意味が違うものを同じ型で扱う」危険が生まれる。
///
///          **共通化するのは編集インターフェースだけ**。`ITransformSource` を実装して
///          いるので、ギズモ・インスペクタ・Undo/Redo は
///          `GetComponent<ITransformSource>()` で `TransformComponent` と区別なく引ける。
///          これにより、以前は `dynamic_cast` の分岐から漏れていて
///          **ギズモが効かなかった UIImage / ParticleSystem でもギズモが効くようになる**。
class EulerTransformComponent : public IComponent, public ITransformSource {
public:
    const char* GetTypeName() const override { return "EulerTransform"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "トランスフォーム"; }
#endif

    // ===== ITransformSource =====

    Vector3& Translate() override { return transform_.translate; }
    Vector3& Rotate()    override { return transform_.rotate; }
    Vector3& Scale()     override { return transform_.scale; }

    // ===== アクセサ =====

    EulerTransform& Get() { return transform_; }
    const EulerTransform& Get() const { return transform_; }

private:
    EulerTransform transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
};
}
