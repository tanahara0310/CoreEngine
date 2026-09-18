#pragma once

#include <optional>

class asIScriptEngine;

namespace CoreEngine::Script
{
    class ScriptGameObject;

    /// @brief 記述子を持つエンジンのコンポーネントを、型名と同じ名前のスクリプトの型にする
    /// @details プロパティは読み書きの関数（`particleSystem.blendMode` など）、記述子の操作はメソッド（`Play()` など）になる。
    ///          GameObject には型名の先頭を小文字にした名前でハンドルを返すプロパティ（`owner.particleSystem`）を足す。
    ///          すでに同じ名前の型があるもの（手で束縛した Transform・UIText・UIImage・Collider）は飛ばす。
    ///          ハンドルは持ち主の GameObject から毎回コンポーネントを引き直し、付いていなければ `exists` が false になる。
    /// @note 他の束縛（GameObject・UI・当たり判定）より後に呼ぶ。コンポーネントの型の一覧はファクトリから取る。
    /// @return すべて登録できたら true
    bool RegisterComponentBinding(asIScriptEngine* engine);

    /// @brief `GetComponent(?&out)` の受け口：エンジンのコンポーネントの型なら、付いていればハンドルを入れる
    /// @param reference ハンドルの置き場
    /// @param typeId 置き場の型 ID
    /// @return エンジンのコンポーネントの型でなければ std::nullopt。型なら、付いていたかどうか
    std::optional<bool> GetEngineComponent(ScriptGameObject& owner, void* reference, int typeId);
}
