#pragma once

#include "externals/nlohmann/single_include/nlohmann/json_fwd.hpp"

/// @brief エディタ設定自動保存のセクションインターフェース

namespace CoreEngine
{
    class EditorSettingsSubsystem;

    /// @brief エディタ設定自動保存の 1 セクション（＝1 ファイル）を表すインターフェース
    /// @details EditorSettingsSubsystem に登録すると、登録時に保存済み JSON から Deserialize され、
    ///          以降はポーリング差分検知により変更のたびに自動保存される。
    ///          シーン/GameObject の明示保存（SceneSaveSystem）とは独立した仕組み。
    ///          設計書: Docs/Engine/Editor/EditorSettingsAutoSave_Design.md
    class IEditorSettingsSection
    {
    public:
        /// @note 登録されたまま破棄された場合はストアから自己解除する
        ///       （定義は EditorSettingsSubsystem.cpp）
        virtual ~IEditorSettingsSection();

        /// @brief セクション名（保存ファイル名 "{name}.json" になる）
        virtual const char* GetSectionName() const = 0;

        /// @brief 現在の設定値を JSON へ書き出す
        /// @param out 書き出し先 JSON オブジェクト
        virtual void Serialize(nlohmann::json& out) const = 0;

        /// @brief JSON から設定値を復元する
        /// @param in 読み込み元 JSON オブジェクト
        /// @note JsonManager::SafeGet を使い、欠損キーは現在値を維持すること（後方互換）
        virtual void Deserialize(const nlohmann::json& in) = 0;

    private:
        friend class EditorSettingsSubsystem;

        // 登録先ストア（RegisterSection / UnregisterSections が設定・解除する）。
        // 所有者が解除し忘れたままセクションが破棄されても、デストラクタがここ経由で
        // 自己解除するため、ストア側にダングリングポインタが残らない
        EditorSettingsSubsystem* registeredStore_ = nullptr;
    };
}
