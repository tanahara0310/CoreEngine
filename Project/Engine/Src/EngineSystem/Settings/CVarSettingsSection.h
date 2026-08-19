#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @file
/// @brief CVar の自動保存セクション

namespace CoreEngine
{
    /// @brief 登録済み CVar をまとめて自動保存するセクション
    /// @details 保存先は接頭辞で 2 分される。"d." は個人状態（Saved 配下）、
    ///          それ以外はプロジェクト設定（Config 配下）。CVarFlags::NoSave は対象外。
    /// @note 復元対象は登録時点で存在する CVar だけ。CVar は必ずファイルスコープで定義すること。
    class CVarSettingsSection : public IEditorSettingsSection
    {
    public:
        /// @param userStatePart true = 個人状態パート（"d." のみ・Saved 配下）
        ///                      false = プロジェクト設定パート（"d." 以外・Config 配下）
        explicit CVarSettingsSection(bool userStatePart)
            : userStatePart_(userStatePart) {}

        const char* GetSectionName() const override
        {
            return userStatePart_ ? "EditorState" : "CVars";
        }

        StorageArea GetStorageArea() const override
        {
            return userStatePart_ ? StorageArea::UserSaved : StorageArea::ProjectConfig;
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

        // CVar はレジストリが変更/確定の通番を持つため、ポーリング不要のイベント駆動で保存する
        ChangeSignal GetChangeSignal() const override { return ChangeSignal::Revision; }
        uint64_t GetChangeRevision() const override;
        uint64_t GetCommitRevision() const override;

        /// @brief 旧形式（Saved/EditorSettings/CVars.json に全部入り）を新 2 層へ移行する
        /// @details 新しい Config 側ファイルが無く旧ファイルがある場合のみ、キーを
        ///          接頭辞で振り分けて両ファイルを生成し、旧ファイルは .migrated へ退避する
        ///          （ユーザーデータは削除しない）。セクション登録より前に 1 回呼ぶこと
        static void MigrateLegacyFile();

        /// @brief 既定値から上書きされている CVar の一覧をログへ出す
        /// @details 差分保存では「コード既定値を変えても保存済みの古い値が黙って勝つ」ため、
        ///          何が上書きされているかを毎起動で可視化する。両セクションの復元後に 1 回呼ぶ
        static void LogOverriddenCVars();

    private:
        bool userStatePart_ = false;
    };
}
