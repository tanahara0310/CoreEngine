#pragma once
#include <memory>

#include "WinApp/WinApp.h"
#include "Utility/Debug/LeakChecker.h"
#include "Utility/Debug/CrashDump.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/EngineConfig.h"

/// @brief フレームワーク基底クラス - エンジン層の汎用機能を提供
/// ゲーム固有のクラスはこのクラスを継承して実装する

namespace CoreEngine
{
class StartupSequence;

/// @brief ゲーム側が継承するアプリケーションの骨格（初期化・ゲームループ・終了処理）
class Framework {
public:
    Framework() = default;
    /// @brief デストラクタ（派生シーンより先にエンジンを壊さないよう .cpp で定義する）
    virtual ~Framework();

    /// @brief フレームワークの実行（ゲームループ）
    void Run();

protected:
    // ──────────────────────────────────────────────────────────
    // 仮想関数 - 派生クラスでオーバーライドする
    // ──────────────────────────────────────────────────────────

    /// @brief 初期化処理（ゲーム固有のリソース読み込み等）
    virtual void Initialize() = 0;

    /// @brief ゲーム固有の初期化を起動シーケンスへステップとして積む
    /// @details 既定では Initialize() 全体を 1 ステップとして積む。
    ///          シーン構築のように数秒かかる処理を持つゲームは、
    ///          これをオーバーライドして細かく割ること。1 ステップの実行時間が
    ///          そのままローディング画面の固まる時間になる。
    virtual void BuildStartupTasks(StartupSequence& sequence);

    /// @brief アセットの非同期先読みを起動シーケンスへ積む（省略可）
    /// @details ModelManager 生成直後・シェーダコンパイル群より前に挿入される。
    /// @note 積むステップは「ワーカーへ投げて即座に戻る」ものにすること。
    ///       ここで同期的に待つと順番が前に来るだけで何も速くならない。
    virtual void BuildPreloadTasks(StartupSequence& /*sequence*/) {}

    /// @brief 終了処理（ゲーム固有のリソース解放等）
    virtual void Finalize() = 0;

    /// @brief 更新処理（ゲーム固有のロジック）
    virtual void Update() = 0;

    /// @brief 描画前準備（描画キュー構築など、毎フレーム実行）
    /// @note 実際の描画コマンド発行はエンジン側の RenderGraph
    ///       （EngineSystem::ExecuteRenderPipeline）が行うので、
    ///       ゲーム側が用意するのは「何を描くか」の登録だけ。
    virtual void PrepareRender() {}

    // ──────────────────────────────────────────────────────────
    // エンジンシステムへのアクセス
    // ──────────────────────────────────────────────────────────

    /// @brief エンジンシステムの取得
    /// @return エンジンシステムへのポインタ
    EngineSystem* GetEngineSystem() { return engineSystem_.get(); }

private:
    /// @brief ローディング画面を出しながら起動シーケンスを最後まで進める
    /// @param sequence 実行する起動シーケンス
    /// @param config   エンジン設定（ローディング画面の見出しに使う）
    void RunStartupSequence(StartupSequence& sequence, const EngineConfig& config);

    // ──────────────────────────────────────────────────────────
    // エンジン層の汎用データ（どのゲームでも使う）
    // ──────────────────────────────────────────────────────────

    /// @brief エンジンシステム（DirectX, 入力, オーディオ等の管理）
    std::unique_ptr<EngineSystem> engineSystem_;

    /// @brief ウィンドウアプリケーション
    std::unique_ptr<WinApp> winApp_;

    /// @brief メモリリークチェッカー（デバッグビルドのみ）
    std::unique_ptr<LeakChecker> leakChecker_;

    /// @brief ゲームループ終了フラグ
    bool isEndRequest_ = false;
};
}
