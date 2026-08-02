#pragma once

#include "externals/nlohmann/single_include/nlohmann/json_fwd.hpp"
#include <string_view>

/// @brief CVar の JSON シリアライズ

namespace CoreEngine
{
    /// @brief 登録済み CVar を JSON へ書き出す / JSON から復元する共通処理
    /// @details 自動保存（CVarSettingsSection）と名前付きプリセット
    ///          （PostEffectPresetManager）の両方から使う。キーは CVar の
    ///          フルネーム（"r.Bloom.Intensity"）をそのまま使うフラット形式。
    ///          設計: Docs/Engine/Editor/CVar_Design.md
    namespace CVarSerialization
    {
        /// @brief CVar を JSON へ書き出す
        /// @param out          書き出し先
        /// @param prefix       対象の接頭辞（空文字なら全件）
        /// @param skipDefaults true でコードデフォルトのままの項目を書かない
        /// @details 自動保存では skipDefaults=true にして「触った項目だけ」を残す。
        ///          プリセットは完全なスナップショットが必要なので false にする。
        ///          CVarFlags::NoSave が付いた変数は常に対象外
        void Save(nlohmann::json& out, std::string_view prefix = "", bool skipDefaults = false);

        /// @brief JSON から CVar を復元する
        /// @param in     読み込み元
        /// @param prefix 対象の接頭辞（空文字なら全件）
        /// @note 存在しないキーは現在値を維持する（後方互換）
        void Load(const nlohmann::json& in, std::string_view prefix = "");
    }
}
