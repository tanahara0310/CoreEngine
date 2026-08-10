#include "pch.h"
#include "EditorSettingsSubsystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace CoreEngine
{
    namespace
    {
        constexpr int kSettingsVersion = 1;

        /// @brief 現在時刻を "HH:MM:SS" 形式で返す（管理パネルの最終保存時刻表示用）
        std::string FormatNowTime()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            char buffer[16]{};
            std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
            return buffer;
        }
    }

    // 登録されたまま破棄されたセクションの自己解除。
    // 所有者が UnregisterSections を呼び忘れても、ストア側に
    // ダングリングポインタが残らないことを保証する
    IEditorSettingsSection::~IEditorSettingsSection()
    {
        if (registeredStore_) {
            registeredStore_->UnregisterSection(this);
        }
    }

    // ===== パスヘルパー =====

    std::string EditorSettingsSubsystem::GetSettingsDir(IEditorSettingsSection::StorageArea area) const {
        // SceneSaveSystem の "Application/Assets/Scenes" と同じく実行時カレント基準。
        // 2 層化（UE の Config/ と Saved/Config/ の分離に相当）:
        // - ProjectConfig: 較正値などプロジェクト資産。git にコミットして共有
        // - UserSaved: カメラ位置など個人の作業状態。git 管理外
        return area == IEditorSettingsSection::StorageArea::ProjectConfig
            ? "Application/Config/EngineSettings"
            : "Application/Saved/EditorSettings";
    }

    std::string EditorSettingsSubsystem::GetFilePath(const IEditorSettingsSection* section) const {
        return GetSettingsDir(section->GetStorageArea()) + "/" + section->GetSectionName() + ".json";
    }

    std::string EditorSettingsSubsystem::GetTempPath(const IEditorSettingsSection* section) const {
        return GetFilePath(section) + ".tmp";
    }

    std::string EditorSettingsSubsystem::GetBackupPath(const IEditorSettingsSection* section) const {
        return GetSettingsDir(section->GetStorageArea()) + "/_backup/"
             + std::string(section->GetSectionName()) + ".json.bak";
    }

    // ===== ライフサイクル =====

    void EditorSettingsSubsystem::Initialize(EngineSystem* /*engine*/, const EngineConfig& /*config*/)
    {
        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSettingsDir(IEditorSettingsSection::StorageArea::UserSaved));
        jm.CreateJsonDirectory(GetSettingsDir(IEditorSettingsSection::StorageArea::ProjectConfig));
        lastCheckTime_ = std::chrono::steady_clock::now();
    }

    void EditorSettingsSubsystem::Finalize()
    {
        FlushAll();
        // ストアより長生きするセクションが自己解除で死んだストアに触れないよう、
        // バックリンクを切ってからエントリを破棄する
        for (auto& entry : entries_) {
            entry.section->registeredStore_ = nullptr;
        }
        entries_.clear();
    }

    void EditorSettingsSubsystem::EndFrame()
    {
        const auto now = std::chrono::steady_clock::now();

        // Polling 方式のセクションを見るのは 1 秒に 1 回だけ（従来動作）
        const bool pollNow =
            std::chrono::duration<double>(now - lastCheckTime_).count() >= kCheckIntervalSec;
        if (pollNow) {
            lastCheckTime_ = now;
        }

        for (auto& entry : entries_) {
            if (entry.section->GetChangeSignal() != IEditorSettingsSection::ChangeSignal::Revision) {
                if (pollNow) {
                    SaveIfChanged(entry);
                }
                continue;
            }

            // ---- Revision 方式（イベント駆動）。毎フレーム整数比較 2 回だけ ----
            const uint64_t revision = entry.section->GetChangeRevision();
            if (revision != entry.lastSeenRevision) {
                entry.lastSeenRevision = revision;
                entry.lastChangeTime = now;  // 変更を観測 → デバウンス計時を開始/延長
                entry.dirty = true;
            }

            // 確定（スライダーを離す等）はデバウンスを待たず同フレームで書く
            const uint64_t commit = entry.section->GetCommitRevision();
            const bool committed = (commit != entry.lastSeenCommitRevision);
            entry.lastSeenCommitRevision = commit;

            if (!entry.dirty) {
                continue;
            }
            const bool quiet =
                std::chrono::duration<double>(now - entry.lastChangeTime).count() >= kDebounceSec;
            if (committed || quiet) {
                // 書き込み失敗時のみ dirty を維持して次フレーム再試行する
                entry.dirty = !SaveIfChanged(entry);
            }
        }
    }

    // ===== セクション登録 =====

    void EditorSettingsSubsystem::RegisterSection(IEditorSettingsSection* section, const void* owner)
    {
        if (!section) {
            return;
        }
        if (section->registeredStore_) {
            std::cerr << "[EditorSettings] Section '" << section->GetSectionName()
                      << "' is already registered. Ignored." << std::endl;
            return;
        }

        Entry entry;
        entry.section = section;
        entry.owner = owner;

        // Deserialize 前の状態を「コードデフォルト」として確保（Reset to Default の復元元）
        section->Serialize(entry.defaultSnapshot);

        // 保存済みファイルがあれば即復元
        auto& jm = JsonManager::GetInstance();
        const std::string filePath = GetFilePath(section);
        if (jm.FileExists(filePath)) {
            const json loaded = jm.LoadJson(filePath);
            if (!loaded.is_null() && !loaded.empty()) {
                section->Deserialize(loaded);
            }
        }

        // 復元後の状態を差分比較の基準にする（次に実際の変更が起きるまで書き込まない）
        section->Serialize(entry.lastSaved);

        // Revision 方式の基準通番も復元後の値で初期化する。
        // Deserialize が復元で通番を進めるため、ここで揃えないと起動直後に
        // 「変更あり」と誤検知して無意味な保存が走る
        entry.lastSeenRevision = section->GetChangeRevision();
        entry.lastSeenCommitRevision = section->GetCommitRevision();

        section->registeredStore_ = this;
        entries_.push_back(std::move(entry));
    }

    void EditorSettingsSubsystem::UnregisterSections(const void* owner)
    {
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (it->owner == owner) {
                SaveIfChanged(*it); // 解除前に最終保存（シーン遷移時の取りこぼし防止）
                it->section->registeredStore_ = nullptr;
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void EditorSettingsSubsystem::UnregisterSection(IEditorSettingsSection* section)
    {
        // ~IEditorSettingsSection からの自己解除経路。派生クラスは既に破棄済みのため
        // Serialize（最終保存）は呼べない（直近ポーリング時点の保存内容が残る）
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->section == section) {
                section->registeredStore_ = nullptr;
                entries_.erase(it);
                return;
            }
        }
    }

    void EditorSettingsSubsystem::FlushAll()
    {
        for (auto& entry : entries_) {
            SaveIfChanged(entry);
        }
    }

    // ===== 管理操作 =====

    bool EditorSettingsSubsystem::ResetSectionToDefault(const std::string& sectionName)
    {
        Entry* entry = FindEntry(sectionName);
        if (!entry) {
            return false;
        }
        entry->section->Deserialize(entry->defaultSnapshot);
        SaveIfChanged(*entry); // リセット結果を即座にファイルへ反映
        return true;
    }

    bool EditorSettingsSubsystem::RestoreSectionBackup(const std::string& sectionName)
    {
        Entry* entry = FindEntry(sectionName);
        if (!entry) {
            return false;
        }

        auto& jm = JsonManager::GetInstance();
        const std::string backupPath = GetBackupPath(entry->section);
        if (!jm.FileExists(backupPath)) {
            return false;
        }
        const json loaded = jm.LoadJson(backupPath);
        if (loaded.is_null() || loaded.empty()) {
            return false;
        }
        entry->section->Deserialize(loaded);
        SaveIfChanged(*entry);
        return true;
    }

    // ===== 内部処理 =====

    EditorSettingsSubsystem::Entry* EditorSettingsSubsystem::FindEntry(const std::string& sectionName)
    {
        for (auto& entry : entries_) {
            if (sectionName == entry.section->GetSectionName()) {
                return &entry;
            }
        }
        return nullptr;
    }

    bool EditorSettingsSubsystem::SaveIfChanged(Entry& entry)
    {
        json current;
        entry.section->Serialize(current);
        if (current == entry.lastSaved) {
            return true;  // 差分なし＝既に永続化されている（値が往復して元に戻った場合を含む）
        }

        if (!WriteAtomic(entry.section, current)) {
            return false;  // 書き込み失敗。呼び出し側が再試行を判断する
        }
        entry.lastSaved = std::move(current);
        entry.lastSaveTime = FormatNowTime();
        return true;
    }

    std::vector<EditorSettingsSubsystem::SectionStatus> EditorSettingsSubsystem::GetSectionStatuses() const
    {
        auto& jm = JsonManager::GetInstance();
        std::vector<SectionStatus> statuses;
        statuses.reserve(entries_.size());
        for (const auto& entry : entries_) {
            SectionStatus status;
            status.name = entry.section->GetSectionName();
            status.fileExists = jm.FileExists(GetFilePath(entry.section));
            status.backupExists = jm.FileExists(GetBackupPath(entry.section));
            status.lastSaveTime = entry.lastSaveTime;
            statuses.push_back(std::move(status));
        }
        return statuses;
    }

    bool EditorSettingsSubsystem::WriteAtomic(const IEditorSettingsSection* section, const json& data)
    {
        namespace fs = std::filesystem;

        const std::string settingsDir = GetSettingsDir(section->GetStorageArea());
        const std::string filePath = GetFilePath(section);
        const std::string tempPath = GetTempPath(section);
        const std::string backupPath = GetBackupPath(section);

        json out = data;
        out["version"] = kSettingsVersion;

        try {
            fs::create_directories(settingsDir);

            // 1. 一時ファイルへ全量書き込み（本体は書き込み途中のクラッシュから守る）
            {
                std::ofstream file(tempPath, std::ios::trunc);
                if (!file.is_open()) {
                    std::cerr << "[EditorSettings] Failed to open temp file: " << tempPath << std::endl;
                    return false;
                }
                file << out.dump(4);
                if (!file.good()) {
                    std::cerr << "[EditorSettings] Failed to write temp file: " << tempPath << std::endl;
                    return false;
                }
            }

            // 2. 旧ファイルを 1 世代バックアップへ退避（誤変更の復元用）
            std::error_code ec;
            if (fs::exists(filePath, ec)) {
                fs::create_directories(settingsDir + "/_backup", ec);
                fs::copy_file(filePath, backupPath, fs::copy_options::overwrite_existing, ec);
                // バックアップ失敗は本保存を妨げない（初回起動や読み取り専用時など）
            }

            // 3. rename による置換（既存ファイルは上書き）
            fs::rename(tempPath, filePath);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[EditorSettings] Error saving section '" << section->GetSectionName()
                      << "': " << e.what() << std::endl;
            return false;
        }
    }
}
