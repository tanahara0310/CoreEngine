#pragma once

#include "EngineSystem/Subsystem/IEngineSubsystem.h"
#include "EngineSystem/Settings/IEditorSettingsSection.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

/// @file
/// @brief エディタ設定の自動保存サブシステム

namespace CoreEngine
{
    /// @brief エディタ設定（大気・雲・水・デバッグカメラ等）を変更のたびに自動保存するサブシステム
    /// @details 登録された IEditorSettingsSection を ChangeSignal 別に保存する。
    ///          Revision 方式は 0.3 秒デバウンス＋確定時は即時、Polling 方式は 1 秒間隔の差分比較。
    ///          いずれもアトミック書き込み（tmp → rename）で、セクション単位にファイルを分ける。
    class EditorSettingsSubsystem : public IEngineSubsystem
    {
    public:
        const char* GetName() const noexcept override { return "EditorSettingsSubsystem"; }

        /// @brief 初期化（保存ディレクトリの確保）
        void Initialize(EngineSystem* engine, const EngineConfig& config) override;

        /// @brief 終了処理（全セクションの最終保存）
        void Finalize() override;

        /// @brief フレーム終了処理（Revision 方式は毎フレーム検知、Polling 方式は 1 秒間隔）
        void EndFrame() override;

        // ──────────────────────────────────────────────────────────
        // セクション登録
        // ──────────────────────────────────────────────────────────

        /// @brief セクションを登録する
        /// @param section セクション（所有権は呼び出し側。Unregister まで生存させること）
        /// @param owner   登録元の識別子（UnregisterSections での一括解除に使う）
        /// @note 登録時にコードデフォルトを控えたうえで、保存済みファイルがあれば即 Deserialize が走る
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
        std::string GetSettingsDirForDisplay() const
        {
            return GetSettingsDir(IEditorSettingsSection::StorageArea::ProjectConfig)
                 + " ／ " + GetSettingsDir(IEditorSettingsSection::StorageArea::UserSaved);
        }

    private:
        /// @brief 登録済みセクションのエントリ
        struct Entry {
            IEditorSettingsSection* section = nullptr;
            const void* owner = nullptr;
            nlohmann::json defaultSnapshot;  // 登録時（Deserialize 前）のコードデフォルト
            nlohmann::json lastSaved;        // 最後に保存（または復元）した内容。差分比較の基準
            std::string lastSaveTime;        // このセッションで最後に書き込んだ時刻（表示用）

            // ---- Revision 方式（イベント駆動）の状態 ----
            uint64_t lastSeenRevision = 0;        // 前フレームまでに観測した変更通番
            uint64_t lastSeenCommitRevision = 0;  // 前フレームまでに観測した確定通番
            std::chrono::steady_clock::time_point lastChangeTime{};  // 最後に変更を観測した時刻
            bool dirty = false;                   // 変更を観測してからまだ保存していない
        };

        // ===== パスヘルパー（保存先はセクションの StorageArea で決まる） =====
        std::string GetSettingsDir(IEditorSettingsSection::StorageArea area) const;
        std::string GetFilePath(const IEditorSettingsSection* section) const;
        std::string GetTempPath(const IEditorSettingsSection* section) const;
        std::string GetBackupPath(const IEditorSettingsSection* section) const;

        /// @brief エントリをシリアライズし、前回保存分と差分があればアトミック書き込みする
        /// @return 現在の状態が永続化されている場合 true（書き込み成功、または差分なし）。
        ///         false は書き込み失敗のみ（呼び出し側は dirty を維持して次フレーム再試行する）
        bool SaveIfChanged(Entry& entry);

        /// @brief tmp 書き込み → 旧ファイルを .bak へ退避 → rename で置換
        bool WriteAtomic(const IEditorSettingsSection* section, const nlohmann::json& data);

        /// @brief セクション名でエントリを検索
        Entry* FindEntry(const std::string& sectionName);

        std::vector<Entry> entries_;

        // Polling 方式の間隔（ミラー系。カメラ移動中などに書き込みすぎないための下限）
        static constexpr double kCheckIntervalSec = 1.0;
        // Revision 方式のデバウンス: 最後の変更からこれだけ静かになったら書く。
        // ドラッグ中の毎フレーム変更やプリセット読込の一括変更を 1 回の書き込みに合流させる
        static constexpr double kDebounceSec = 0.3;
        std::chrono::steady_clock::time_point lastCheckTime_{};
    };
}
