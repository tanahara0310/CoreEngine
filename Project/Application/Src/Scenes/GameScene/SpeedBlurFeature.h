#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief トロッコの速さに合わせて、画面のモーションブラーを強くする Feature を作る
    /// @details GameScene::OnInitialize() から `AddFeature(CreateSpeedBlurFeature())` の
    ///          1 行で登録する。列車は Feature が自分で探して繋ぐ。
    ///
    ///          速度計は画面左上にしかないので、数字を見ていないプレイヤーには
    ///          速さの変化が届かない。ブラーなら画面全体が速さを言うので、
    ///          加速していく気持ちよさと、駅で失う瞬間の両方が視線の位置によらず伝わる。
    ///
    /// @note 使うのは速度ベクタ方式のモーションブラー（r.MotionBlur.*）で、
    ///       ラジアルブラーではない。このシーンのカメラは俯角 60 度でヨーが 0、
    ///       トロッコは +X（画面の横方向）へ走るので、画面中心から外へ流れる
    ///       ラジアルブラーは流れる向きが実際の動きと合わない。速度ベクタ方式なら
    ///       向きは G-Buffer の動きから決まるうえ、カメラが追っているトロッコは
    ///       画面上でほぼ止まっているので、地面だけが流れて主役は素通しで残る。
    ///
    /// @note シーンにいる間だけエンジン側の設定（r.MotionBlur.ShutterAngle と有効フラグ）を
    ///       借りて、シーンを抜けるときに借りた時点の値へ戻す。他のシーンへは持ち出さない。
    ///
    /// @note 調整値は SpeedBlurFeature.cpp のファイルスコープにある `Game.SpeedBlur.*` の
    ///       CVar 群。CVars.json へ自動保存され、インスペクターの「ゲーム設定」から編集できる。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateSpeedBlurFeature();
}
