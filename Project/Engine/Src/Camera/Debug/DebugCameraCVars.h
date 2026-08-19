#pragma once

/// @file
/// @brief エディタ視点カメラ（軌道操作つき）の設定・姿勢を CVar で永続化する

namespace CoreEngine
{
    class Camera;
    class OrbitFlyController;

    /// @brief エディタ視点カメラの CVar 同期
    /// @details 値の実体はコントローラとカメラにあり、UI・マウス操作・シーンコードから
    ///          書き換わるため、CVar は実体を鏡写しにする。
    ///          `RestoreTo` が CVar → 実体（初期化時 1 回）、`MirrorFrom` が実体 → CVar（毎フレーム）。
    /// @note CVar はすべて `CVarFlags::NoUI`（自動生成 UI は毎フレームのミラーに上書きされるため）。
    namespace DebugCameraCVars
    {
        /// @brief 保存済みの設定・姿勢をカメラとコントローラへ復元する
        /// @note 呼び出し後に姿勢と行列の反映まで行う（次フレームの Update を待たない）
        void RestoreTo(Camera& camera, OrbitFlyController& controller);

        /// @brief 現在の設定・姿勢を CVar へ写す
        void MirrorFrom(const Camera& camera, const OrbitFlyController& controller);
    }
}
