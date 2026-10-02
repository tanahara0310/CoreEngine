#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
class EngineSystem;

/// @brief シーンの設定（足した Feature・既定の床・衝突マトリクス）を見て変えるパネル
/// @note ここで変えた値は Ctrl+S でシーンのマニフェスト（`_scene.json`）へ保存される。
namespace SceneSettingsPanel
{
    /// @brief 開いているシーンの設定を描く（シーンは描くたびにエンジンから引き直す）
    void Draw(EngineSystem& engine);
}
}

#endif // CORE_EDITOR
