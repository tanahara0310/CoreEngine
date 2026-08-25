#pragma once

#include <d3d12.h>
#include <string_view>

/// @file
/// @brief GPU クラッシュ（デバイスロスト）の原因を DRED から取り出してログへ落とす
/// @note DRED の有効化はデバイス生成より前に行うこと（後からでは効かない）

namespace CoreEngine
{
    /// @brief DRED を有効化する（デバイス生成より前に呼ぶこと）
    /// @details Auto-Breadcrumbs と PageFault を両方 FORCED_ON にする（わずかなオーバーヘッドあり）
    /// @return 有効化できたら true（OS / ドライバが DRED 非対応なら false）
    bool EnableDeviceRemovedExtendedData();

    /// @brief デバイスが失われていれば、その原因をログへ書き出す
    /// @param device 調べるデバイス（nullptr なら何もしない）
    /// @param whenLabel 検出地点（"EndFrame::Present" など）。ログの先頭に出る
    /// @return デバイスが失われていれば true（＝呼び出し側は続行できない）
    /// @details 失われていなければ何もしないので毎フレーム呼んでよい。書き出し後に Flush() する
    bool ReportIfDeviceRemoved(ID3D12Device* device, std::string_view whenLabel);
}
