#pragma once

#include <d3d12.h>
#include <string_view>

/// @file
/// @brief GPU クラッシュ（デバイスロスト）の原因をログへ落とすための DRED 連携
///
/// @details
/// GPU 側でハング・ページフォルトが起きると D3D12 は「デバイスが失われた」状態になり、
/// 以後すべての API が失敗する。既定でアプリから分かるのは HRESULT の理由コードだけで、
/// **どのコマンドで死んだか・どのリソースを踏んだか** は分からない。
///
/// DRED（Device Removed Extended Data）を有効にしておくと、ドライバが
/// 「各コマンドリストのどの命令まで GPU が完了したか」（Auto-Breadcrumbs）と
/// 「ページフォルトした仮想アドレスの周辺にあった／直前に解放されたリソース」
/// （PageFault Allocation）を記録してくれる。ここはその取り出しとログ整形を担当する。
///
/// @note DRED の有効化は **デバイス生成より前** に行う必要がある（後からでは効かない）。

namespace CoreEngine
{
    /// @brief DRED を有効化する（デバイス生成より前に呼ぶこと）
    /// @details Auto-Breadcrumbs（命令単位の進捗）と PageFault（踏んだ仮想アドレス）を
    ///          両方 FORCED_ON にする。有効中はドライバが命令ごとに記録を書くため
    ///          わずかなオーバーヘッドがある。既定では設定でオフ。
    /// @return 有効化できたら true（OS / ドライバが DRED 非対応なら false）
    bool EnableDeviceRemovedExtendedData();

    /// @brief デバイスが失われていれば、その原因をログへ書き出す
    /// @param device 調べるデバイス（nullptr なら何もしない）
    /// @param whenLabel 検出地点（"EndFrame::Present" など）。ログの先頭に出る
    /// @return デバイスが失われていれば true（＝呼び出し側は続行できない）
    /// @details 失われていなければ何もせず false を返すので、毎フレーム呼んでよい。
    ///          レポートは Logger の Graphics/Device カテゴリへ出し、最後に Flush() する
    ///          （この直後にプロセスが死んでも内容がディスクに残るように）。
    bool ReportIfDeviceRemoved(ID3D12Device* device, std::string_view whenLabel);
}
