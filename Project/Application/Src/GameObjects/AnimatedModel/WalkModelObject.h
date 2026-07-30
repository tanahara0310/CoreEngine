#pragma once

#include "GameObject/Model/AnimatedModelObject.h"
#include <string>

namespace CoreEngine {
    class IParticleSystem;
}

/// @brief Walkモデルオブジェクト
class WalkModelObject : public CoreEngine::AnimatedModelObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "WalkModel"; }

    /// @brief 手から出すパーティクルシステムを登録する
    /// @param system 追従させるパーティクルシステム（所有権は持たない。nullptr で解除）
    /// @param jointName 発生位置にするジョイント名（例: "mixamorig:LeftHand"）
    /// @note エミッター位置はアニメーション更新の直後に書き込むため、
    ///       手の動きに対して 1 フレームも遅れない。
    void SetHandParticleSystem(CoreEngine::IParticleSystem* system, const std::string& jointName);

protected:
    std::string GetModelPath() const override { return "walk.gltf"; }
    std::string GetAnimationName() const override { return "walkAnimation"; }
    std::string GetTexturePath() const override { return "white.png"; }
    void OnInitialize() override;

    /// @brief アニメーション更新後にパーティクルのエミッター位置を手へ移す
    void OnAnimationUpdated() override;

private:
    /// 手に追従させるパーティクルシステム（所有権は持たない）
    CoreEngine::IParticleSystem* handParticleSystem_ = nullptr;

    /// パーティクルの発生位置にするジョイント名
    std::string handJointName_;
};
