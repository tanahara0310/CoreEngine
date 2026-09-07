#pragma once

#include "Camera/Rig/CameraRigTypes.h"

#include <string>
#include <vector>

/// @file
/// @brief カメラリグの JSON 保存・読み込み
/// @details エディタ専用ではない。製品ビルドでもリグを読めるよう USE_IMGUI の外に置く。

namespace CoreEngine
{
    /// @brief カメラリグの保存/読み込みを担当する I/O ヘルパー
    class CameraRigIO {
    public:
        /// @brief リグディレクトリ内の json 一覧を取得（ファイル名のみ・昇順）
        /// @param directoryPath 探索するディレクトリ（存在しなければ空を返す）
        static std::vector<std::string> GetRigFileList(const std::string& directoryPath);

        /// @brief リグを json へ保存
        /// @return 書き込みに成功したら true
        static bool Save(const std::string& filePath, const CameraRigAsset& asset);

        /// @brief json からリグを読み込む
        /// @details 読み込めた場合のみ outAsset を書き換える。欠けたキーは既定値で補うので、
        ///          後から項目を足しても古いファイルはそのまま読める。読み込み後に
        ///          Sanitize を通すため、壊れた値が入っていても評価できる状態になる。
        /// @return ファイルを開けて中身があれば true
        static bool Load(const std::string& filePath, CameraRigAsset& outAsset);
    };
}
