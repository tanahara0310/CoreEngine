#pragma once

#include "externals/nlohmann/single_include/nlohmann/json_fwd.hpp"

#include <cstdint>

/// @file
/// @brief エディタ視点カメラ（軌道操作つき）の設定・姿勢の控え

namespace CoreEngine
{
    class Camera;
    class OrbitFlyController;

    /// @brief エディタ視点カメラの設定・姿勢・投影をエンジンの寿命で控える
    /// @details 値の実体はコントローラとカメラにあり、UI・マウス操作・シーンコードから書き換わる。
    ///          シーンを作るたびに `RestoreTo` で控えを当て、毎フレーム `Capture` で控え直す。
    ///          次の起動への持ち越しは、エディタ設定の保存項目が `Save` / `Load` で行う。
    namespace DebugCameraState
    {
        /// @brief 控えをカメラとコントローラへ当てる
        /// @note 呼び出し後に姿勢と行列の反映まで行う（次フレームの Update を待たない）
        void RestoreTo(Camera& camera, OrbitFlyController& controller);

        /// @brief 今の設定・姿勢・投影を控える（変わっていたら通番を進める）
        void Capture(const Camera& camera, const OrbitFlyController& controller);

        /// @brief 控えを JSON へ書き出す
        void Save(nlohmann::json& out);

        /// @brief JSON から控えを読む（無いキーは今の値のまま）
        void Load(const nlohmann::json& in);

        /// @brief 控えが変わるたびに増える通番
        uint64_t GetRevision();
    }
}
