#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Script
{
    /// @brief 描画まわりの関数をスクリプトへ登録する
    /// @details 描画の設定を読むだけの名前空間 `Rendering`（自動露出の EV）を出す。
    ///          マテリアル（`owner.material`）は記述子からの自動の束縛（ComponentBinding）が出す。
    /// @param engineSystem 描画のサービスを引く先（nullptr なら読み取りは既定値を返す）
    /// @return すべて登録できたら true
    bool RegisterRenderingBinding(asIScriptEngine* engine, EngineSystem* engineSystem);
}
