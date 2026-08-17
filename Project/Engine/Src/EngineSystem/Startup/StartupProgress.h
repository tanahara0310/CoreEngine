#pragma once
#include <functional>

/// @brief 起動シーケンス中に「重いステップの内側」から進捗を報告するための細い口。
///
/// 起動が「ハングに見える」直接の原因は、初期化中にウィンドウメッセージを一度も
/// 処理しないこと。StartupSequence はステップの切れ目でメッセージを処理するが、
/// シェーダ 119 本のコンパイルのように 1 ステップが数秒かかるものは切れ目だけでは足りず、
/// Windows のハング検出（無応答 5 秒）に引っかかる。
/// そういう長いループの内側からこの Tick を呼ぶと、スプラッシュの再描画と
/// メッセージ処理が走り、無応答扱いを免れる。
///
/// @note 起動シーケンス外では sink が空なので Tick はほぼ無コスト（空判定 1 回）。
/// @note sink を設定したスレッド以外からの Tick は無視する。GDI とメッセージポンプは
///       ウィンドウを作ったスレッドの持ち物で、ワーカースレッドから触ってはいけない。
namespace CoreEngine::StartupProgress
{
    /// @brief 進捗の受け口。detail はステップ内の細かい進行内容（UTF-8 / nullptr 可）
    using Sink = std::function<void(const char* detail)>;

    /// @brief 受け口を設定する（呼んだスレッドが所有者になる）
    void SetSink(Sink sink);

    /// @brief 受け口を解除する。起動シーケンス完了時に必ず呼ぶこと
    void ClearSink();

    /// @brief 起動シーケンス実行中かどうか
    bool IsActive() noexcept;

    /// @brief 進捗を報告する（起動シーケンス外では何もしない）
    /// @param detail 表示したい細目（シェーダ名・テクスチャ名など）。省略時は表示を変えない
    void Tick(const char* detail = nullptr);
}
