#pragma once

/// @brief Math/Geometry（純関数）の自己テスト
/// @details 形状交差・レイ交差の入出力を既知値で固定し、実装を差し替えても
///          答えが変わらないことを保証する。結果は CollisionTest::Report に
///          "U1"〜 の ID で登録される（Phase 0 から ID は据え置き）。
namespace CollisionTest
{
    /// @brief 全ての純関数テストを実行して Report へ登録する
    /// @note シーン初期化時に 1 回だけ呼ぶ（毎フレーム走らせる必要はない）
    void RunGeometrySelfTest();
}
