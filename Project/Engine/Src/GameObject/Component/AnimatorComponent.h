#pragma once

#include "GameObject/Component/IComponent.h"
#include "GameObject/Component/MeshRendererComponent.h"
#include "GameObject/Component/TransformComponent.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"

#include <optional>
#include <string>
#include <vector>

namespace CoreEngine
{
/// @brief スケルトンアニメーションを駆動するコンポーネント
///
/// @details アニメーションの実体（`AnimationPlayer`）は `Model` が所有しており、
///          ここが持つのは「毎フレーム進める」「クリップを切り替える」
///          「ジョイントのワールド行列を教える」という操作の側。
///          モデルは兄弟の `MeshRendererComponent` から引く。
///
///          **これがあると継承が要らなくなる**: 以前は
///          `GameObject → ModelGameObject → AnimatedModelObject` と 3 段継承しないと
///          アニメーションを持てなかった。そのため「プリミティブメッシュ ＋
///          ジョイント追従」という組み合わせ（`WeaponObject`）が表現できず、
///          追従元を生ポインタで持つ回避策が発生していた。
///          今は `AddComponent<AnimatorComponent>()` と
///          `AddComponent<SkeletonSocketComponent>()` を必要なオブジェクトに載せるだけ。
class AnimatorComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "Animator"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "アニメーション"; }
#endif

    // ===== ライフサイクル =====

    /// @brief 兄弟のメッシュ描画・トランスフォームを捕まえる
    void Start() override;

    /// @brief アニメーションを 1 フレーム進める
    /// @note `GameObject::Update()`（派生クラスの `OnUpdate()`）より**前**に走る。
    ///       ジョイント追従のような「更新後の姿勢を読む」処理は
    ///       `SkeletonSocketComponent` のように `LateUpdate()` で行うこと
    ///       （`GameObjectManager` が全オブジェクトの Update 完了後にまとめて回すので、
    ///       生成順に依存せず最新の姿勢が読める）。
    void Update() override;

    // ===== クリップ切り替え =====

    /// @brief アニメーションを即座に切り替える
    /// @return 成功したら true
    bool Switch(const std::string& clipName, bool loop = true);

    /// @brief アニメーションをブレンドしながら切り替える
    /// @details 内部では `AnimationBlender` が現在姿勢と切り替え先姿勢をジョイント単位で
    ///          補間する（平行移動・スケールは Lerp、回転は Slerp）。
    bool SwitchWithBlend(const std::string& clipName, float blendDuration = 0.3f, bool loop = true);

    /// @brief 現在再生中のクリップ識別名
    const std::string& GetCurrentClipName() const { return currentClipName_; }

    /// @brief 現在のクリップ名を設定する（初期化時に呼ぶ）
    void SetCurrentClipName(const std::string& name) { currentClipName_ = name; }

    /// @brief 登録済みクリップの識別名を列挙する
    std::vector<std::string> GetClipNames() const;

    // ===== ジョイント参照 =====
    // 骨のデバッグ表示・武器のソケットアタッチ・ジョイント追従パーティクルは
    // すべてこの 3 つを土台にしている。

    /// @brief 再生中のスケルトン（アニメーションを持たない場合は nullptr）
    const Skeleton* GetSkeleton() const;

    /// @brief ジョイントのワールド行列
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    /// @return スケルトンが無い／名前が見つからない場合は std::nullopt
    /// @details ジョイントが持つのはモデルローカルな「スケルトン空間行列」なので、
    ///          オブジェクトのワールド行列を掛けてワールド空間へ持ち上げる。
    std::optional<Matrix4x4> GetJointWorldMatrix(const std::string& jointName) const;

    /// @brief ジョイントのワールド座標
    std::optional<Vector3> GetJointWorldPosition(const std::string& jointName) const;

    // ===== 骨のデバッグ表示 =====

    void SetSkeletonDebugDrawEnabled(bool enabled) { skeletonDebugDrawEnabled_ = enabled; }
    bool IsSkeletonDebugDrawEnabled() const { return skeletonDebugDrawEnabled_; }

    /// @brief スケルトンの親子関係を線で描画する
    void DrawSkeletonDebugLines() const;

private:
    /// @brief 兄弟コンポーネントが未取得なら取りに行く（Start 前に呼ばれた場合の保険）
    void ResolveSiblings() const;

    /// @brief 再生中の AnimationPlayer（無ければ nullptr）
    class AnimationPlayer* GetPlayer() const;

    mutable MeshRendererComponent* renderer_ = nullptr;
    mutable TransformComponent* transform_ = nullptr;

    /// 現在再生中のクリップ識別名
    std::string currentClipName_;

    /// 骨のデバッグ表示フラグ
    bool skeletonDebugDrawEnabled_ = false;
};
}
