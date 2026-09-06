#pragma once

#include "Camera/Sequence/CameraSequenceTypes.h"

#include <string>
#include <vector>

/// @file
/// @brief カメラシーケンスの JSON 保存・読み込み
/// @details エディタ専用ではない。製品ビルドでもシーケンスを読めるよう USE_IMGUI の外に置く。

namespace CoreEngine
{
    /// @brief カメラシーケンスの保存/読み込みを担当する I/O ヘルパー
    class CameraSequenceIO {
    public:
        /// @brief シーケンスディレクトリ内の json 一覧を取得（ファイル名のみ・昇順）
        /// @param directoryPath 探索するディレクトリ（存在しなければ空を返す）
        static std::vector<std::string> GetSequenceFileList(const std::string& directoryPath);

        /// @brief シーケンスを json へ保存
        /// @return 書き込みに成功したら true
        static bool Save(const std::string& filePath, const CameraSequenceAsset& asset);

        /// @brief json からシーケンスを読み込む
        /// @details 読み込めた場合のみ outAsset を書き換える。欠けたキーは既定値で補うため、
        ///          古いバージョンのファイルもそのまま読める。
        /// @return キーフレームが 1 つ以上取れたら true
        static bool Load(const std::string& filePath, CameraSequenceAsset& outAsset);
    };
}
