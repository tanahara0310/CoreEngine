#pragma once

#include "Math/Vector/Vector3.h"

#include <memory>

namespace CoreEngine {
    class ISceneFeature;
}

namespace GameComponents
{
    /// @brief 岩を壊した瞬間に散る破片（モデルパーティクル）の Feature を作る
    /// @details GameScene::OnInitialize() から `AddFeature(CreateRockBreakDebrisFeature())` で登録する。
    ///          シーンに particle.obj のモデルパーティクルを 1 つ置き、
    ///          PlayRockBreakDebris() が呼ばれるたびに数個だけ弾けさせる。
    /// @note 見た目と動きの値は Assets/Presets/Particle/RockBreakDebris.json が持つ。
    ///       実行中はインスペクタの「RockBreakDebris」で調整して、そのまま上書き保存できる。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateRockBreakDebrisFeature();

    /// @brief 砕けた岩の周りへ破片を出す
    /// @param impactPosition 岩の足元（投石の着弾位置）のワールド座標
    /// @note Feature が登録されていないシーンでは何もしない。
    void PlayRockBreakDebris(const CoreEngine::Vector3& impactPosition);
}
