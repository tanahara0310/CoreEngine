#pragma once

#include "EngineSystem/Subsystem/IEngineSubsystem.h"
#include "EngineSystem/Settings/IEditorSettingsSection.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"
#include <chrono>
#include <string>
#include <vector>

/// @brief エディタ設定の自動保存サブシステム

namespace CoreEngine
{
    /// @brief エディタ設定（大気・雲・水・デバッグカメラ等）を変更のたびに自動保存するサブシステム
    /// @details 登録された IEditorSettingsSection を一定間隔でシリアライズし、前回保存分と
    ///          差分があるセクションだけをアトミック書き込み（tmp → rename）で保存する。
    ///          保存先はセクション単位のファイル分割: Application/Saved/EditorSettings/{Section}.json。
    ///          シーン/GameObject の明示保存フローには一切関与しない。
    ///          設計書: Docs/Engine/Editor/EditorSettingsAutoSave_Design.md
    class EditorSettingsSubsystem : public IEngineSubsystem
    {
    public:
        const char* GetName() const noexcept override { return "EditorSettingsSubsystem"; }

        /// @brief 初期化（保存ディレクトリの確保）
        void Initialize(EngineSystem* engine, const EngineConfig& config) override;

        /// @brief 終了処理（全セクションの最終保存）
        void Finalize() override;

        /// @brief フレーム終了処理（一定間隔で差分検知＋保存）
        void EndFrame() override;

        // ──────────────────────────────────────────────────────────
        // セクション登録
        // ──────────────────────────────────────────────────────────

        /// @brief セクションを登録する
        /// @details 登録時に「コードデフォルト」スナップショットを取得した後、保存済みファイルが
        ///          あれば即 Deserialize が走る（＝前回状態の復元）。シーン寿命のオブジェクトは
        ///          シーン初期化時に登録すればよい。
        /// @param section セクション（所有権は呼び出し側。Unregister まで生存させること）
        /// @param owner 登録元の識別子（UnregisterSections での一括解除に使用）
        void RegisterSection(IEditorSettingsSection* section, const void* owner);

        /// @brief owner が登録した全セクションを解除する（解除前に最終保存を行う）
        /// @param owner RegisterSection に渡した識別子
        void UnregisterSections(const void* owner);

        /// @brief 単一セクションを解除する（~IEditorSettingsSection からの自己解除用）
        /// @details この経路が呼ばれる時点では派生クラスが既に破棄されているため、
        ///          最終保存（Serialize）は行わない。最終保存が必要な通常の解除は
        ///          UnregisterSections を使うこと。
        void UnregisterSection(IEditorSettingsSection* section);

        /// @brief 全セクションを即時保存する（差分があるもののみ）
        void FlushAll();

        // ──────────────────────────────────────────────────────────
        // 管理操作（Engine Settings の管理パネルから使用）
        // ──────────────────────────────────────────────────────────

        /// @brief セクションを起動時のコードデフォルト値へ戻す
        /// @return セクションが見つかり復元できた場合 true
        bool ResetSectionToDefault(const std::string& sectionName);

        /// @brief セクションをバックアップ（前回保存分 .bak）から復元する
        /// @return バックアップが存在し復元できた場合 true
        bool RestoreSectionBackup(const std::string& sectionName);

        /// @brief 管理パネル表示用のセクション状態
        struct SectionStatus {
            std::string name;           ///< セクション名（＝ファイル名）
            bool fileExists = false;    ///< 保存ファイルが存在するか
            bool backupExists = false;  ///< バックアップ（.bak）が存在するか
            std::string lastSaveTime;   ///< このセッションで最後に書き込んだ時刻（空 = 未保存）
        };

        /// @brief 登録中の全セクションの状態を取得する（管理パネル用）
        std::vector<SectionStatus> GetSectionStatuses() const;

        /// @brief 保存ディレクトリのパスを取得する（管理パネルの表示用）
        std::string GetSettingsDirForDisplay() const { return GetSettingsDir(); }

    private:
        /// @brief 登録済みセクションのエントリ
        struct Entry {
            IEditorSettingsSection* section = nullptr;
            const void* owner = nullptr;
            nlohmann::json defaultSnapshot;  // 登録時（Deserialize 前）のコードデフォルト
            nlohmann::json lastSaved;        // 最後に保存（または復元）した内容。差分比較の基準
            std::string lastSaveTime;        // このセッションで最後に書き込んだ時刻（表示用）
        };

        // ===== パスヘルパー =====
        std::string GetSettingsDir() const;
        std::string GetFilePath(const std::string& sectionName) const;
        std::string GetTempPath(const std::string& sectionName) const;
        std::string GetBackupPath(const std::string& sectionName) const;

        /// @brief エントリをシリアライズし、前回保存分と差分があればアトミック書き込みする
        /// @return 書き込みを行った場合 true
        bool SaveIfChanged(Entry& entry);

        /// @brief tmp 書き込み → 旧ファイルを .bak へ退避 → rename で置換
        bool WriteAtomic(const std::string& sectionName, const nlohmann::json& data);

        /// @brief セクション名でエントリを検索
        Entry* FindEntry(const std::string& sectionName);

        std::vector<Entry> entries_;

        // ポーリング間隔管理
        static constexpr double kCheckIntervalSec = 1.0;
        std::chrono::steady_clock::time_point lastCheckTime_{};
    };
}
