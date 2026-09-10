#pragma once

#include <memory>

namespace CoreEngine {
    class ISceneFeature;
}

namespace GameComponents
{
    /// @brief サルがバナナの木から実をもぎ取り、頭上へ掲げるまでを見せる Feature を作る
    /// @details GameScene::OnInitialize() から `AddFeature(CreateBananaHarvestEffectFeature())` の
    ///          1 行で登録する。スタミナ・列車・マップの各コンポーネントは Feature が
    ///          自分で探して繋ぐので、シーン側から渡すものは無い。
    ///
    ///          バナナは「サル 1 匹が木 1 本を通り過ぎるたび」に 1 本ずつ出る。
    ///          この発火は列車が 1 マス進むたびに車両ごとへ順番に届くので、
    ///          列車が長いほど収穫が長く続く。サルの数を数字ではなく
    ///          「バナナが飛んだ回数」と「収穫が続いた長さ」で見せるための Feature。
    ///
    /// @note 見た目と時間の値はすべて CVar `Game.BananaHarvest.*` が持つ。
    ///       実行中にインスペクターの「ゲーム設定」から調整でき、CVars.json へ自動保存される。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateBananaHarvestEffectFeature();
}
