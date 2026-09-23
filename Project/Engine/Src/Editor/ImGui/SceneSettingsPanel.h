#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
class EngineSystem;

/// @brief シーンの設定（足した Feature・既定の床・衝突マトリクス）を見て変えるパネル
/// @note ここで変えた値は Ctrl+S でシーンのマニフェスト（`_scene.json`）へ保存される。
namespace SceneSettingsPanel
{
    /// @brief Engine Settings へパネルを登録する（プロセス中 1 回だけ実行される）
    /// @note ドロワーはエンジンだけを覚え、シーンは描くたびに引き直す
    ///       （パネルの登録解除の口が無いので、シーンの寿命に縛られるものを持たない）。
    void EnsureRegistered(EngineSystem* engine);
}
}

#endif // CORE_EDITOR
