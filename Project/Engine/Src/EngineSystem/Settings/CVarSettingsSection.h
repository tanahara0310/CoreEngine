#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief CVar の自動保存セクション

namespace CoreEngine
{
    /// @brief 登録済み CVar をまとめて自動保存するセクション
    /// @details 機能ごとに IEditorSettingsSection を実装する必要をなくすのが目的。
    ///          CVar を 1 つ定義すれば、ここへ何も追記しなくても保存・復元される。
    ///
    ///          保存先は 2 層（Phase 4。UE の Config/ と Saved/Config/ の分離に相当）:
    ///          - プロジェクト設定（"d." 以外 = r./sys.）
    ///            → Application/Config/EngineSettings/CVars.json（git にコミットして共有）
    ///          - 個人の作業状態（"d." = カメラ等）
    ///            → Application/Saved/EditorSettings/EditorState.json（git 管理外）
    ///          区分は接頭辞のみで決まる（命名規則 r./d./sys. がそのまま分類になる）。
    ///
    ///          キーは CVar のフルネーム（"r.Vignette.Intensity"）をそのまま使うフラット形式。
    ///          CVarFlags::NoSave が付いた変数は対象外。
    ///
    /// @note このセクションが登録される時点で存在する CVar だけが復元対象になる。
    ///       CVar は静的初期化で構築されるため通常は全て揃っているが、関数内 static や
    ///       動的生成した CVar は復元されないので、必ずファイルスコープで定義すること。
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
