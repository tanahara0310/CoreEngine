#pragma once

#include <memory>

namespace CoreEngine {
    class ISceneFeature;
}

namespace GameComponents
{
    /// @brief ゲームシーンの突入演出と、200m 刻みの目標提示をまとめて受け持つ Feature を作る
    ///
    /// @details GameScene::OnInitialize() から `AddFeature(CreateGameEntranceFeature())` の
    ///          1 行で登録する。列車・トーンマップ・距離目盛りは Feature が自分で探して繋ぐので、
    ///          シーン側から渡すものは無い。
    ///
    ///          **突入演出（雲海ブレイク → もくひょう看板 → つなげ！！）**
    ///          1. 雲の中から始まる。高さフォグの雲海を画面の上まで持ち上げた状態で開幕し、
    ///             それを島の下まで沈めることで「雲を抜けて降りる」を作る。
    ///          2. カメラは真上のリグ（`Entrance_Sky`）から始まり、通常の `GamePlay` リグへ
    ///             ブレンドして降りる。構図は Presets/CameraRigs/ の json 側が持つ。
    ///          3. ツタで吊るした木の看板が降りてきて、最初の目標「500ｍ」を提示する。
    ///          4. 締めに「つなげ！！」を叩き込んで演出を終える。
    ///
    ///          雲の中にいる間はプレイヤーの操作を止める。止めるのはレールカーソル
    ///          （`RailBuilderComponent`）だけで、白幕が晴れてワールドが見えた時点で返す。
    ///          列車は最初のレールが敷かれるまで発車しないので、これで演出中は
    ///          ゲームが進まない。
    ///
    ///          **目標の進行**
    ///          列車が目標地点を越えるたびに、地面の距離目盛り（`MapViewComponent` が置く
    ///          `DistanceMarker_*`）のうち目標の目盛りへ色が付き、次の目標の看板が降りてくる。
    ///          500m → 1000m → 1500m … と `Game.Goal.StepMeters` 刻みで、上限なく続く。
    ///          目盛りの色は「到達済み＝緑」「次の目標＝赤（脈打つ）」「それ以外＝白のまま」。
    ///
    /// @note 距離の単位は地面の目盛りと同じで、ワールド座標 X がそのままメートル。
    ///       列車の絶対位置で判定するので、看板の数字と足元の目盛りの数字が必ず一致する。
    ///
    /// @note 調整値はすべて CVar が持つ（`Game.Entrance.*` と `Game.Goal.*`、
    ///       看板の見た目は `Game.ObjectiveSign.*`）。インスペクターの「ゲーム設定」から
    ///       編集でき、CVars.json へ自動保存される。`Game.Entrance.Enabled` を切ると
    ///       雲海とカメラ演出だけを飛ばし、看板と目標の進行はそのまま残る。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateGameEntranceFeature();
}
