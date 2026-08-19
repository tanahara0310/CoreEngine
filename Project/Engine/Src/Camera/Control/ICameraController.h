#pragma once

#include "Camera/Control/CameraInputState.h"

/// @file
/// @brief カメラの動かし方（操作方法）を表す抽象

namespace CoreEngine
{
    class Camera;

    /// @brief 「カメラをどう動かすか」だけを担当するコントローラのインターフェース
    /// @details Update() が触ってよいのは対象カメラの Transform のみ。投影や GPU リソースには触れない。
    ///          入力は正規化済みの CameraInputState だけを見る。
    /// @note 1 つのカメラに付くコントローラは 1 つ（CameraManager が保証する）。
    class ICameraController {
    public:
        virtual ~ICameraController() = default;

        /// @brief 入力を処理し、結果を対象カメラの Transform へ書き出す
        /// @param input 正規化済み入力（active == false なら状態を変えないこと）
        /// @param deltaTime 前フレームからの経過秒
        /// @param camera 操作対象
        virtual void Update(const CameraInputState& input, float deltaTime, Camera& camera) = 0;

        /// @brief 現在の内部状態を対象カメラへ反映する（入力処理なし）
        /// @details 設定復元直後など「入力を経ずに姿勢を確定させたい」ときに使う。
        virtual void ApplyTo(Camera& camera) const = 0;

        /// @brief 初期状態へ戻す
        virtual void Reset() = 0;

        /// @brief 表示名（エディタ UI 用）
        virtual const char* GetDisplayName() const = 0;
    };
}
