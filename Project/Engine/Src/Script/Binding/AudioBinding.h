#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class AudioSystem;
}

namespace CoreEngine::Script
{
    /// @brief 音の型と関数をスクリプトへ登録する
    /// @details 列挙 `AudioBus`、値型 `PlayParams`、最後の参照が消えると止まる再生のハンドル `Sound`、
    ///          名前空間 `Audio` の再生口を出す。
    /// @param audio 鳴らす先（nullptr なら再生口は何もしない）
    /// @return すべて登録できたら true
    bool RegisterAudioBinding(asIScriptEngine* engine, AudioSystem* audio);
}
