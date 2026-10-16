#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief カメラリグの切り替えをスクリプトへ登録する
    /// @details 名前空間 `CameraRig` に、名前でリグを動かす・止める・今のリグを調べる口を出す。
    /// @return すべて登録できたら true
    bool RegisterCameraRigBinding(asIScriptEngine* engine);
}
