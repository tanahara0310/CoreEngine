#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief カメラの揺れの型と関数をスクリプトへ登録する
    /// @details 列挙 `ShakeWaveform` / `ShakeSpace` / `ShakeTimeMode`、値型 `CameraShakeParams`、
    ///          名前空間 `CameraShake` の再生口と、名前空間 `CameraShakePresets` の既定値を出す。
    ///          列挙 `EaseType` と値型 `Vector3` を先に登録しておくこと。
    /// @return すべて登録できたら true
    bool RegisterCameraShakeBinding(asIScriptEngine* engine);
}
