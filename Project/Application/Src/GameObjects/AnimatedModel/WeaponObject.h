#pragma once

#include "GameObject/Model/AnimatedModelObject.h"
#include "GameObject/Primitive/PrimitiveGameObject.h"
#include <string>

/// @brief キャラクターの手に持たせる武器（ジョイントソケットアタッチのデモ）
///
/// AnimatedModelObject::GetJointWorldMatrix() で得たジョイントのワールド行列に
/// ソケットオフセットを掛けて、自分のワールド行列を毎フレーム上書きする。
/// これだけで「武器が手に追従する」挙動になる。
///
/// @note 追従元キャラクターより後に生成すること。
///       GameObjectManager は登録順に Update するため、先に生成しないと
///       1 フレーム前のスケルトン姿勢を参照してしまう。
class WeaponObject : public CoreEngine::PrimitiveGameObject {
public:
    const char* GetObjectName() const override { return "Weapon"; }

    /// @brief 追従させるジョイントを指定する
    /// @param owner  追従元のスキニングモデル（所有権は持たない）
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
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

    /// @brief ジョイント追従（TransferMatrix の後に呼ばれるのでワールド行列を上書きできる）
    void OnUpdate() override;

private:
    /// 追従元のキャラクター（nullptr のときは追従しない）
    const CoreEngine::AnimatedModelObject* owner_ = nullptr;

    /// 追従先のジョイント名
    std::string jointName_;

    // ソケットオフセット（ジョイントローカル）
    CoreEngine::Vector3 socketTranslate_ = { 0.0f, 0.0f, 0.0f };
    CoreEngine::Vector3 socketRotate_ = { 0.0f, 0.0f, 0.0f };
    CoreEngine::Vector3 socketScale_ = { 1.0f, 1.0f, 1.0f };

    /// ジョイント行列のスケール／位置を 1 度だけログに出したか（リグ単位系の確認用）
    bool loggedJointScale_ = false;
};
