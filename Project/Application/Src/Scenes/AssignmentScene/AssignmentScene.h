#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

namespace CoreEngine {
    class IParticleSystem;
}

// GameObject
#include "GameObjects/AnimatedModel/WalkModelObject.h"
#include "GameObjects/AnimatedModel/BrainStemObject.h"
#include "GameObjects/AnimatedModel/WeaponObject.h"
#include "GameObjects/AnimatedModel/FoxObject.h"

/// @brief 課題提出用シーン
/// @details スキニング（ComputeShader スキニング）・スケルトン・アニメーションまわりの
///          加点要素をまとめて実演するためのシーン。
///          Phase 1: スキニングモデルの表示 / CS スキニング / アニメーション補間
namespace CoreEngine
{
    class AssignmentScene : public BaseScene {
    public:
        /// @brief シーン固有の初期化
        void OnInitialize() override;

        /// @brief 描画
        void Draw() override;

    protected:
        /// @brief 更新
        void OnUpdate() override;

    private:
        /// スキニングアニメーションを再生するキャラクター（所有権は GameObjectManager）
        WalkModelObject* character_ = nullptr;

        /// マルチメッシュ・マルチマテリアルのスキニングモデル（所有権は GameObjectManager）
        BrainStemObject* multiMaterialModel_ = nullptr;

        /// 右手のジョイントに追従する武器（所有権は GameObjectManager）
        WeaponObject* weapon_ = nullptr;

        /// 左手から出す GPU パーティクル（所有権は GameObjectManager）
        CoreEngine::IParticleSystem* handParticles_ = nullptr;

        /// アニメーションブレンド実証用のキツネ（所有権は GameObjectManager）
        FoxObject* fox_ = nullptr;
    };
}
