#pragma once

#include "Camera/Control/CameraInputState.h"

/// @brief カメラの動かし方（操作方法）を表す抽象

namespace CoreEngine
{
    class Camera;

    /// @brief カメラコントローラ
    /// @details 「カメラがどう動かされるか」だけを担当する。カメラ本体は姿勢と投影の
    ///          データにすぎず、操作方法を持たない。
    ///
    ///          契約:
    ///          - Update() が触ってよいのは対象カメラの Transform（位置・回転・スケール）だけ。
    ///            投影パラメータや GPU リソースには触れない。
    ///          - 1 つのカメラに付くコントローラは 1 つ。CameraManager が保証する。
    ///            （以前は DebugCamera 内蔵の操作・GameView 操作モジュール・追従モジュールが
    ///              同じカメラを別々に書き換えており、どれが勝つか未定義だった）
    ///          - 入力は CameraInputState として正規化済みのものだけを見る。
    ///            ImGui / InputManager / ウィンドウには依存しない。
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
