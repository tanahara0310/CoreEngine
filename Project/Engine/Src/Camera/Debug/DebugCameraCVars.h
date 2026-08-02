#pragma once

/// @brief エディタ視点カメラ（軌道操作つき）の設定・姿勢を CVar で永続化する

namespace CoreEngine
{
    class Camera;
    class OrbitFlyController;

    /// @brief エディタ視点カメラの CVar 同期
    /// @details 値の実体はコントローラ（操作設定・軌道状態）とカメラ（投影パラメータ）にあり、
    ///          どちらも**シーン寿命**で、カメラ UI・マウス操作・シーンコードから書き換わる。
    ///          そのため CVar 側を唯一のソースにはできず、実体を鏡写しにする方式を採る。
    ///
    ///          - `RestoreTo`  : CVar → 実体（シーン初期化時に 1 回）
    ///          - `MirrorFrom` : 実体 → CVar（毎フレーム。ここを通ることで保存対象に載る）
    ///
    ///          CVar はすべて `CVarFlags::NoUI`。自動生成 UI を出すと、毎フレームの
    ///          ミラーに即座に上書きされて「触れるのに効かない UI」になるため。
    ///          編集はカメラエディタ（操作設定・カメラパラメータ）が担当する。
    namespace DebugCameraCVars
    {
        /// @brief 保存済みの設定・姿勢をカメラとコントローラへ復元する
        /// @note 呼び出し後に姿勢と行列の反映まで行う（次フレームの Update を待たない）
        void RestoreTo(Camera& camera, OrbitFlyController& controller);

        /// @brief 現在の設定・姿勢を CVar へ写す
        void MirrorFrom(const Camera& camera, const OrbitFlyController& controller);
    }
}
