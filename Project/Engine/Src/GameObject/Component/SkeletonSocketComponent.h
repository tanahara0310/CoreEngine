#pragma once

#include "GameObject/Component/AnimatorComponent.h"
#include "GameObject/Component/IComponent.h"
#include "GameObject/Component/TransformComponent.h"
#include "Math/Vector/Vector3.h"

#include <string>

namespace CoreEngine
{
/// @brief 他オブジェクトのジョイントへ自分を追従させるコンポーネント
///
/// @details 剣を手に持たせる、パーティクルの発生源を手に置く、といった
///          「ソケットアタッチ」を担う。
///
///          **これが継承の限界に対する答え**: 以前は `WeaponObject` が
///          @code
///          class WeaponObject : public PrimitiveGameObject {
///              const AnimatedModelObject* owner_ = nullptr;  // 追従元を生ポインタで保持
///              void OnUpdate() override;                     // TransferMatrix の後に行列を上書き
///          @endcode
///          と書かれていた。「プリミティブメッシュ ＋ ジョイント追従」を継承で
///          表現できないため、片方を継承・片方を生ポインタにするしかなかった。
///
///          **生成順の罠も消える**: 旧実装のヘッダには
///          「追従元キャラクターより後に生成すること。GameObjectManager は登録順に
///          Update するため、先に生成しないと 1 フレーム前のスケルトン姿勢を参照してしまう」
///          という注意書きがあった。このコンポーネントは `LateUpdate()` で動き、
///          `GameObjectManager` は**全オブジェクトの Update が終わってから**
///          LateUpdate をまとめて回すので、生成順に関係なく必ず今フレームの姿勢を読める。
///
///          **スケールは引き継がない**: ジョイント行列はリグの単位系によって
///          スケールを含む（Mixamo は cm 単位なので 0.01 倍が入る）。そのまま掛けると
///          武器が極小になって消えるため、位置と回転だけを取り出して使う。
class SkeletonSocketComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "SkeletonSocket"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "ソケット追従"; }
#endif

    // ===== 設定 =====

    /// @brief 追従先を指定する
    /// @param animator 追従元の `AnimatorComponent`（所有権は持たない。nullptr で解除）
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    void Attach(const AnimatorComponent* animator, const std::string& jointName) {
        animator_ = animator;
        jointName_ = jointName;
    }

    /// @brief ジョイントから見た相対姿勢（ソケットオフセット）を設定する
    /// @param translate ジョイントローカルでの位置ずらし [m]
    /// @param rotate    ジョイントローカルでの回転（ラジアン）
    /// @param scale     スケール
    void SetOffset(const Vector3& translate, const Vector3& rotate,
                   const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
        offsetTranslate_ = translate;
        offsetRotate_ = rotate;
        offsetScale_ = scale;
    }

    /// @brief 追従が有効か（追従元とジョイント名が揃っているか）
    bool IsAttached() const { return animator_ != nullptr && !jointName_.empty(); }

    // ===== ライフサイクル =====

    void Start() override { transform_ = Sibling<TransformComponent>(); }

    /// @brief ジョイントのワールド行列にオフセットを掛けて自分のワールド行列を上書きする
    /// @note `LateUpdate()` なのは追従元のアニメーション更新（`AnimatorComponent::Update()`）が
    ///       全オブジェクト分終わった後に読む必要があるため。
    void LateUpdate() override;

private:
    /// 追従元（所有権は持たない）
    const AnimatorComponent* animator_ = nullptr;

    /// 追従先のジョイント名
    std::string jointName_;

    // ソケットオフセット（ジョイントローカル）
    Vector3 offsetTranslate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetRotate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetScale_{ 1.0f, 1.0f, 1.0f };

    TransformComponent* transform_ = nullptr;
};
}
