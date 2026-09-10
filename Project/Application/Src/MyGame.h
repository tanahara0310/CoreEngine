#pragma once
#include "EngineSystem/Framework.h"
#include "Scene/SceneManager.h"
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

    /// @brief ゲーム固有の初期化を起動シーケンスへ 3 ステップに割って積む
    /// @details 初期シーン構築（モデル・テクスチャのロード）だけで数秒かかるので、
    ///          Initialize() を丸ごと 1 ステップにするとローディング画面の進捗が
    ///          そこで止まって見える。
    void BuildStartupTasks(CoreEngine::StartupSequence& sequence) override;

    /// @brief 初期シーンのモデルをシェーダコンパイル中に裏で読み込ませる
    /// @details シーン構築ステップの実測 4.0 秒のうち、Assimp パースと LOD 生成で 2.7 秒。
    ///          どちらもシェーダコンパイルと依存関係が無いので、
    ///          先に走らせておけばクリティカルパスから消せる。
    void BuildPreloadTasks(CoreEngine::StartupSequence& sequence) override;

    /// @brief ゲーム固有の終了処理
    void Finalize() override;

    /// @brief ゲーム固有の更新処理（シーン更新を委譲）
    void Update() override;

    /// @brief 描画前準備（シーン描画キュー構築を委譲）
    void PrepareRender() override;

private:
    // ──────────────────────────────────────────────────────────
    // 初期化ステップ（Initialize と BuildStartupTasks の共通実体）
    // ──────────────────────────────────────────────────────────

    /// @brief SceneManager を生成し、全シーンを登録する
    void CreateSceneManager();

    /// @brief 初期シーンを構築する（モデル・テクスチャのロードを含む重い処理）
    void LoadInitialScene();

    /// @brief デバッグ UI に SceneManager を接続する
    void ConnectDebugUI();

    // ──────────────────────────────────────────────────────────
    // ゲーム固有のデータ
    // ──────────────────────────────────────────────────────────

    /// @brief 初期シーン名
    static constexpr const char* kInitialSceneName = "TitleScene";

    /// @brief シーン管理システム
    std::unique_ptr<CoreEngine::SceneManager> sceneManager_;
};
