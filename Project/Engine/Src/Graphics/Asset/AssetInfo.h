#pragma once
#include "AssetType.h"
#include <string>
#include <filesystem>
#include <cstdint>

namespace CoreEngine
{
    // アセット情報
    struct AssetInfo
    {
        std::string guid;                    // 一意なID（変更されない）
        std::string name;                    // ファイル名（拡張子なし）
        std::string fileName;                // ファイル名（拡張子あり）
        std::filesystem::path fullPath;      // 完全なパス
        std::filesystem::path relativePath;  // プロジェクトルートからの相対パス
        AssetType type;                      // アセットの種類
        std::string category;                // カテゴリ（Engine/Application など）
        uint64_t lastModified;               // 最終更新時刻（Unix time）

        AssetInfo()
            : type(AssetType::Unknown)
            , lastModified(0)
        {}
    };
}
