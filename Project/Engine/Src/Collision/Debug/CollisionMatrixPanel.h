#pragma once

#ifdef USE_IMGUI

namespace CoreEngine
{
class EngineSystem;
class CollisionConfig;

/// @brief レイヤー間の衝突マトリクス編集ウィンドウ
/// @details Unity のコリジョンマトリクスに相当する格子状のチェックボックス。
///
///          **永続化しない**。マトリクスはシーンごとに `OnInitialize()` の
///          `SetCollisionEnabled()` で組み立てるものなので、保存した設定で上書きすると
///          「コードに書いてあるのに違う挙動になる」状態を作ってしまう。
///          このパネルは実行中の調整用で、確定した内容はシーンのコードへ書き戻す。
namespace CollisionMatrixPanel
{
    /// @brief Engine Settings へパネルを登録する（プロセス中 1 回だけ実行される）
    /// @note ドロワーは何もキャプチャせず SetActiveConfig() が指す先を読む。
    ///       GameDebugUI にパネルの登録解除 API が無いため、シーンの寿命に
    ///       縛られるポインタをラムダへ捕捉させない。
    void EnsureRegistered(EngineSystem* engine);

    /// @brief 編集対象のマトリクスを差し替える
    /// @param config 現在のシーンのもの。シーン終了時は nullptr を渡すこと。
    void SetActiveConfig(CollisionConfig* config);
}
}

#endif // USE_IMGUI
