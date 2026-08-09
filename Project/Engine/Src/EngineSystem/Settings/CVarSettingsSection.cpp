#include "pch.h"
#include "CVarSettingsSection.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarSerialization.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// @brief 個人状態（UserSaved 側）へ振り分ける接頭辞
        constexpr const char* kUserStatePrefix = "d.";

        // 移行用の固定パス（EditorSettingsSubsystem のディレクトリ規約と一致させること）
        constexpr const char* kLegacyPath = "Application/Saved/EditorSettings/CVars.json";
        constexpr const char* kMigratedPath = "Application/Saved/EditorSettings/CVars.json.migrated";
        constexpr const char* kConfigDir = "Application/Config/EngineSettings";
        constexpr const char* kConfigPath = "Application/Config/EngineSettings/CVars.json";
        constexpr const char* kStatePath = "Application/Saved/EditorSettings/EditorState.json";
    }

    void CVarSettingsSection::Serialize(nlohmann::json& out) const
    {
        // コードデフォルトのままの項目は書かない（skipDefaults）。
        // 全件書くと、後からコード側の既定値を変更しても保存済みの古い値が勝ってしまう。
        // 触った項目だけが並ぶのでファイルが「自分の変更点一覧」にもなる
        if (userStatePart_) {
            CVarSerialization::Save(out, kUserStatePrefix, /*skipDefaults=*/true);
        } else {
            CVarSerialization::Save(out, "", /*skipDefaults=*/true,
                                    /*excludePrefix=*/kUserStatePrefix);
        }
    }

    void CVarSettingsSection::Deserialize(const nlohmann::json& in)
    {
        // ファイルに存在するキーだけが適用される（Config 側と Saved 側でキーは排他）
        CVarSerialization::Load(in);
    }

    uint64_t CVarSettingsSection::GetChangeRevision() const
    {
        return CVarRegistry::Get().GetGlobalRevision();
    }

    uint64_t CVarSettingsSection::GetCommitRevision() const
    {
        return CVarRegistry::Get().GetCommitRevision();
    }

    void CVarSettingsSection::MigrateLegacyFile()
    {
        auto& jm = JsonManager::GetInstance();

        // 新形式が既にあれば何もしない（移行は初回起動の 1 回だけ）
        if (jm.FileExists(kConfigPath) || !jm.FileExists(kLegacyPath)) {
            return;
        }

        const nlohmann::json legacy = jm.LoadJson(kLegacyPath);
        if (legacy.is_null() || !legacy.is_object()) {
            return;
        }

        // キーを接頭辞で振り分ける（"version" は各ファイルの保存時に付き直す）
        nlohmann::json config = nlohmann::json::object();
        nlohmann::json state = nlohmann::json::object();
        for (auto it = legacy.begin(); it != legacy.end(); ++it) {
            if (it.key() == "version") {
                continue;
            }
            if (it.key().rfind(kUserStatePrefix, 0) == 0) {
                state[it.key()] = it.value();
            } else {
                config[it.key()] = it.value();
            }
        }
        config["version"] = 1;
        state["version"] = 1;

        try {
            namespace fs = std::filesystem;
            fs::create_directories(kConfigDir);
            {
                std::ofstream file(kConfigPath, std::ios::trunc);
                file << config.dump(4);
            }
            {
                std::ofstream file(kStatePath, std::ios::trunc);
                file << state.dump(4);
            }
            // 旧ファイルはユーザーデータなので削除せずリネームで退避する
            // （過去に PostEffect 移行でユーザーの有効化設定を失った教訓）
            std::error_code ec;
            fs::rename(kLegacyPath, kMigratedPath, ec);

            Logger::GetInstance().Infof(LogCategory::System,
                "CVar: 旧 CVars.json を 2 層形式へ移行しました（設定 {} 件 → {} / 状態 {} 件 → {}）",
                config.size() - 1, kConfigPath, state.size() - 1, kStatePath);
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Warnf(LogCategory::System,
                "CVar: 旧設定ファイルの移行に失敗しました: {}", e.what());
        }
    }

    void CVarSettingsSection::LogOverriddenCVars()
    {
        // 差分保存の弱点対策: どの CVar が保存値により既定値から上書きされているかを
        // 可視化する。コード側で既定値（較正値）を変更しても、ここに並ぶ項目は
        // 保存済みの古い値が勝ち続けるため、気づけるのはこのログだけ
        std::vector<const ICVar*> overridden;
        for (const ICVar* cvar : CVarRegistry::Get().GetByPrefix("")) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoSave)) {
                continue;
            }
            if (cvar->IsModified()) {
                overridden.push_back(cvar);
            }
        }
        if (overridden.empty()) {
            return;
        }

        auto& logger = Logger::GetInstance();
        logger.Infof(LogCategory::System,
            "CVar: 保存値により {} 個が既定値から上書きされています", overridden.size());
        for (const ICVar* cvar : overridden) {
            logger.Infof(LogCategory::System, "  {} = {} (default {})",
                cvar->GetName(), cvar->ValueToString(), cvar->DefaultToString());
        }
    }
}
