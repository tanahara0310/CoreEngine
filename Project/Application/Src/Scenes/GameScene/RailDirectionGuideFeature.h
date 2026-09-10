#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief レール先頭の「次に伸ばせる向き」を床の矢印で見せる Feature を作る
    /// @details 登録は `AddFeature(GameComponents::CreateRailDirectionGuideFeature())` の 1 行でよい。
    ///          シーンにいる RailPathComponent / MapGeneratorComponent / HungerComponent を
    ///          自分で探して繋ぐので、シーン側からコンポーネントを渡す必要はない。
    /// @note 出すのは 3D テキスト（Text3DObject）で、床と平行に寝かせた矢印を
    ///       先頭マスの上下左右へ 1 つずつ置く。シリアライズ対象から外してあるため
    ///       シーンの JSON には残らない。
    ///       見た目の調整は CVar `Game.RailGuide.*`（インスペクターの「ゲーム設定」）で行う。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateRailDirectionGuideFeature();

    /// @brief 突入演出あけの登場アニメーションの進み具合を渡す
    /// @param reveal 0 = 出さない ／ 1 = 通常の大きさ。間は矢印が床から伸び上がる途中。
    ///               1 を少し超える値を渡すと行き過ぎて戻る（EaseOutBack を通した値を想定）
    /// @details 既定は 1 なので、誰も呼ばなければ従来どおり最初から出る。
    ///          シーンを抜けるときに 1 へ戻るので、次のシーンへは持ち出さない。
    ///          駆動するのは GameEntranceFeature。
    void SetRailDirectionGuideReveal(float reveal);
}
