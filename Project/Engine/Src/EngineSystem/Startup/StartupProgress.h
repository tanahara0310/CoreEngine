#pragma once
#include <functional>

/// @brief 起動シーケンス中に「重いステップの内側」から進捗を報告するための細い口
/// @details ステップの切れ目だけではシェーダコンパイルのような長い処理を跨げず、
///          Windows のハング検出（無応答 5 秒）に引っかかる。長いループの内側から
///          Tick を呼ぶとスプラッシュ再描画とメッセージ処理が走る。
/// @note sink を設定したスレッド以外からの Tick は無視する
///       （GDI とメッセージポンプはウィンドウを作ったスレッドの持ち物）。
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
