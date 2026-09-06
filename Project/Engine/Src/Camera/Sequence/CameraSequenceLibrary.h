#pragma once

#include "Camera/Sequence/CameraSequenceTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// @file
/// @brief シーケンス名からアセットを引くためのキャッシュ付きロード

namespace CoreEngine
{
    /// @brief 名前 → シーケンスの対応を持つ読み込みキャッシュ
    /// @details 再生のたびにディスクを叩かないようにするためのもの。読み込んだアセットは
    ///          shared_ptr で配るので、キャッシュを捨てても再生中の Player は壊れない。
    /// @note メインスレッド専用。
    class CameraSequenceLibrary {
    public:
        /// @brief シーケンスを探すディレクトリを設定する（変更するとキャッシュを捨てる）
        void SetDirectory(const std::string& directoryPath);
        const std::string& GetDirectory() const { return directoryPath_; }

        /// @brief 名前でシーケンスを取得する
        /// @param name 拡張子なしの名前（"Opening_Station"）。".json" 付きでも受ける
        /// @return 見つからない / 読めない場合は nullptr
        std::shared_ptr<const CameraSequenceAsset> Get(const std::string& name);

        /// @brief 指定シーケンスのキャッシュを捨てる（次回 Get で読み直す）
        void Reload(const std::string& name);

        /// @brief キャッシュを全て捨てる
        void ClearCache();

        /// @brief ディレクトリ内のシーケンス名一覧（拡張子なし・昇順）
        std::vector<std::string> ListNames() const;

    private:
        /// @brief 名前をファイルパスへ解決する
        std::string ResolvePath(const std::string& name) const;

        std::string directoryPath_ = CameraSequencePaths::kDirectory;

        // 読み込みに失敗した名前も nullptr で覚える。毎フレーム Play を呼ばれても
        // 存在しないファイルへのアクセスを繰り返さないため。
        std::unordered_map<std::string, std::shared_ptr<const CameraSequenceAsset>> cache_;
    };
}
