#pragma once
#include "../Engine/Framework/Framework.h"
#include "../Engine/Scene/SceneManager.h"
#include <memory>

/// @brief ゲーム固有のアプリケーションクラス
/// Framework を継承し、シーン管理を統合する
class MyGame : public CoreEngine::Framework {
public:
    MyGame() = default;
    ~MyGame() override;

protected:
    // ──────────────────────────────────────────────────────────
    // Framework の仮想関数をオーバーライド
    // ──────────────────────────────────────────────────────────

    /// @brief ゲーム固有の初期化処理
    void Initialize() override;

    /// @brief ゲーム固有の終了処理
    void Finalize() override;

    /// @brief ゲーム固有の更新処理（シーン更新を委譲）
    void Update() override;

    /// @brief ゲーム固有の描画処理（シーン描画を委譲）
    void Draw() override;

private:
    // ──────────────────────────────────────────────────────────
    // ゲーム固有のデータ
    // ──────────────────────────────────────────────────────────

    /// @brief シーン管理システム
    std::unique_ptr<CoreEngine::SceneManager> sceneManager_;
};
