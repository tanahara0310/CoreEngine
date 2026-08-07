#pragma once

#include "GameObject/Model/AnimatedModelObject.h"
#include "GameObject/Primitive/PrimitiveGameObject.h"
#include "GameObject/Component/SkeletonSocketComponent.h"
#include <string>

/// @brief キャラクターの手に持たせる武器（ジョイントソケットアタッチのデモ）
///
/// @details 追従処理そのものは `SkeletonSocketComponent` が持つ。このクラスは
///          「剣の刃を模したプリミティブメッシュ」を作るだけになった。
///
///          **以前は継承で書けなかった**: 「プリミティブメッシュ ＋ ジョイント追従」を
///          表現できないため、`PrimitiveGameObject` を継承しつつ追従元を
///          `const AnimatedModelObject*` の生ポインタで持ち、`OnUpdate()` で
///          ワールド行列を上書きしていた。さらに
///          「追従元キャラクターより後に生成すること（登録順に Update されるため）」
///          という生成順の制約をコメントで人間が担保していた。
///
///          **その制約は無くなった**: `SkeletonSocketComponent` は `LateUpdate()` で動き、
///          `GameObjectManager` は全オブジェクトの Update が終わってから LateUpdate を
///          まとめて回す。したがってどちらを先に生成しても 1 フレーム遅れない。
class WeaponObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @brief コンストラクタ（ソケット追従コンポーネントをアタッチする）
    WeaponObject()
        : socket_(AddComponent<CoreEngine::SkeletonSocketComponent>()) {}

    const char* GetObjectName() const override { return "Weapon"; }

    /// @brief 追従させるジョイントを指定する
    /// @param owner  追従元のスキニングモデル（所有権は持たない）
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    /// @note 生成順は問わない（旧実装の制約は解消済み）。
    void AttachToJoint(const CoreEngine::AnimatedModelObject* owner, const std::string& jointName);

    /// @brief ジョイントから見た相対姿勢（ソケットオフセット）を設定する
    /// @param translate ジョイントローカルでの位置ずらし [m]
    /// @param rotate    ジョイントローカルでの回転（ラジアン）
    /// @param scale     スケール
    void SetSocketOffset(const CoreEngine::Vector3& translate,
        const CoreEngine::Vector3& rotate,
        const CoreEngine::Vector3& scale = { 1.0f, 1.0f, 1.0f });

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

private:
    /// 追従処理を持つコンポーネント（コンストラクタでアタッチ済み・非 nullptr）
    CoreEngine::SkeletonSocketComponent* socket_ = nullptr;
};
