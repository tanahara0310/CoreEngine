#include "pch.h"
#include "WaterSettingsMigration.h"

#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

namespace CoreEngine::WaterSettingsMigration
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* kConfigPath = "Application/Config/EngineSettings/CVars.json";
        constexpr const char* kBackupPath = "Application/Saved/EditorSettings/CVars.json.water.migrated";
        constexpr const char* kSceneRoot = "Application/Assets/Scenes";
        constexpr const char* kAssetRoot = "Application/Assets";
        constexpr const char* kSceneManifestName = "_scene.json";
        constexpr const char* kEnvironmentName = "_environment.json";
        constexpr std::string_view kWaterSurfaceType = "WaterSurface";

        /// @brief 古い CVar の名前と、移した先のキー
        struct KeyMove {
            const char* legacy;
            const char* key;
        };

        /// @brief 水面コンポーネントの保存値へ移す CVar
        constexpr KeyMove kProfileMoves[] = {
            { "r.Water.BaseColor",                "baseColor" },
            { "r.Water.Roughness",                "roughness" },
            { "r.Water.FresnelScale",             "fresnelScale" },
            { "r.Water.FresnelF0",                "fresnelF0" },
            { "r.Water.DepthFadeEnabled",         "depthFadeEnabled" },
            { "r.Water.AbsorptionBase",           "absorption" },
            { "r.Water.ScatteringBase",           "scattering" },
            { "r.Water.Turbidity",                "turbidity" },
            { "r.Water.Caustics.RefractiveIndex", "refractiveIndex" },
            { "r.Water.FFT.Enabled",              "fftEnabled" },
            { "r.Water.FFT.FetchKm",              "fetchKm" },
            { "r.Water.FFT.AmplitudeScale",       "amplitudeScale" },
            { "r.Water.FFT.Choppiness",           "choppiness" },
            { "r.Water.FFT.ActiveComponentCount", "componentCount" },
            { "r.Water.Swell.Enabled",            "swellEnabled" },
            { "r.Water.Swell.Height",             "swellHeight" },
            { "r.Water.Swell.Period",             "swellPeriod" },
            { "r.Water.Swell.Direction",          "swellDirection" },
            { "r.Water.Swell.RelativeWidth",      "swellRelativeWidth" },
            { "r.Water.Swell.Spread",             "swellSpread" },
            { "r.Water.Foam.Enabled",             "foamEnabled" },
            { "r.Water.Foam.WhitecapScale",       "foamWhitecapScale" },
            { "r.Water.Foam.Opacity",             "foamOpacity" },
            { "r.Water.Foam.CascadeWeights",      "foamCascadeWeights" },
            { "r.Water.Foam.DecaySeconds",        "foamDecaySeconds" },
        };

        /// @brief シーンの環境の設定へ移す CVar
        constexpr KeyMove kEnvironmentMoves[] = {
            { "r.Water.FFT.WindSpeed",     "r.Wind.Speed" },
            { "r.Water.FFT.WindDirection", "r.Wind.Direction" },
        };

        /// @brief CVars.json から移す値を集める（移した先のキー → 値）
        template <size_t N>
        json CollectValues(const json& config, const KeyMove (&moves)[N])
        {
            json values = json::object();
            for (const KeyMove& move : moves) {
                const auto found = config.find(move.legacy);
                if (found != config.end()) {
                    values[move.key] = *found;
                }
            }
            return values;
        }

        /// @brief まだ持っていないキーだけ足す
        /// @return 1 つでも足したら true
        bool AddMissing(json& target, const json& values)
        {
            bool changed = false;
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (!target.contains(it.key())) {
                    target[it.key()] = it.value();
                    changed = true;
                }
            }
            return changed;
        }

        /// @brief 保存データの中の水面コンポーネントすべてへ、まだ持っていない水の性質を足す
        /// @return 足した水面の数
        int AddToWaterSurfaces(json& node, const json& values)
        {
            int count = 0;
            if (node.is_object()) {
                const auto type = node.find("type");
                if (type != node.end() && type->is_string() && type->get<std::string>() == kWaterSurfaceType) {
                    json& parameters = node["parameters"];
                    if (!parameters.is_object()) {
                        parameters = json::object();
                    }
                    if (AddMissing(parameters, values)) {
                        ++count;
                    }
                }
                for (auto& child : node) {
                    count += AddToWaterSurfaces(child, values);
                }
            } else if (node.is_array()) {
                for (auto& child : node) {
                    count += AddToWaterSurfaces(child, values);
                }
            }
            return count;
        }

        /// @brief ファイルを丸ごと文字列で読む（読めなければ空）
        std::string ReadText(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return {};
            }
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /// @brief JSON をエンジンのほかの保存と同じ書き方で書く
        bool WriteJson(const fs::path& path, const json& data)
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream file(path);
            if (!file) {
                return false;
            }
            file << JsonManager::ToFileText(data);
            return static_cast<bool>(file);
        }

        /// @brief 水面を持つ保存データのファイルへ、まだ持っていない水の性質を足す
        /// @return 足した水面の数（書けなければ 0）
        int MigrateWaterFile(const fs::path& path, const json& profileValues)
        {
            const std::string text = ReadText(path);
            if (text.find(kWaterSurfaceType) == std::string::npos) {
                return 0;
            }
            json data = json::parse(text, nullptr, false);
            if (data.is_discarded()) {
                return 0;
            }
            const int count = AddToWaterSurfaces(data, profileValues);
            if (count == 0) {
                return 0;
            }
            if (!WriteJson(path, data)) {
                Logger::GetInstance().Warnf(LogCategory::System,
                    "WaterSettingsMigration: 書けませんでした: {}", Logger::GetInstance().PathToUtf8(path));
                return 0;
            }
            return count;
        }

        /// @brief シーンの環境の設定へ、まだ持っていない風を足す
        /// @return 書き換えたら true
        bool MigrateEnvironment(const fs::path& sceneDir, const json& environmentValues)
        {
            const fs::path path = sceneDir / kEnvironmentName;
            json data = json::object();
            if (fs::exists(path)) {
                data = json::parse(ReadText(path), nullptr, false);
                if (data.is_discarded() || !data.is_object()) {
                    return false;
                }
            } else {
                data["version"] = "1.0";
            }
            if (!AddMissing(data, environmentValues)) {
                return false;
            }
            return WriteJson(path, data);
        }

        /// @brief 既にある控えを上書きしない名前を選ぶ
        fs::path ChooseBackupPath()
        {
            const fs::path base = ProjectPaths::Resolve(kBackupPath);
            fs::path path = base;
            for (int n = 2; fs::exists(path); ++n) {
                path = base;
                path += std::format(".{}", n);
            }
            return path;
        }

        /// @brief 移す本体（ファイルの走査の失敗は例外で返る）
        void Migrate()
        {
            const fs::path configPath = ProjectPaths::Resolve(kConfigPath);
            std::error_code ec;
            if (!fs::exists(configPath, ec)) {
                return;
            }

            const std::string configText = ReadText(configPath);
            json config = json::parse(configText, nullptr, false);
            if (config.is_discarded() || !config.is_object()) {
                return;
            }

            const json profileValues = CollectValues(config, kProfileMoves);
            const json environmentValues = CollectValues(config, kEnvironmentMoves);
            if (profileValues.empty() && environmentValues.empty()) {
                return;
            }

            // 元の CVars.json はユーザーの設定なので、書き換える前にそのまま控える
            const fs::path backupPath = ChooseBackupPath();
            fs::create_directories(backupPath.parent_path(), ec);
            {
                std::ofstream backup(backupPath, std::ios::binary);
                backup << configText;
                if (!backup) {
                    Logger::GetInstance().Errorf(LogCategory::System,
                        "WaterSettingsMigration: CVars.json の控えを書けないので、水の設定を移しません: {}",
                        Logger::GetInstance().PathToUtf8(backupPath));
                    return;
                }
            }

            int surfaceCount = 0;
            int environmentCount = 0;
            for (const fs::directory_entry& sceneDir : fs::directory_iterator(ProjectPaths::Resolve(kSceneRoot), ec)) {
                if (!sceneDir.is_directory() || !fs::exists(sceneDir.path() / kSceneManifestName)) {
                    continue;
                }
                if (!profileValues.empty()) {
                    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(sceneDir.path(), ec)) {
                        const fs::path& path = entry.path();
                        if (entry.is_regular_file() && path.extension() == ".json"
                            && !path.filename().wstring().starts_with(L"_")) {
                            surfaceCount += MigrateWaterFile(path, profileValues);
                        }
                    }
                }
                if (!environmentValues.empty() && MigrateEnvironment(sceneDir.path(), environmentValues)) {
                    ++environmentCount;
                }
            }
            if (!profileValues.empty()) {
                for (const fs::directory_entry& entry : fs::recursive_directory_iterator(ProjectPaths::Resolve(kAssetRoot), ec)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".prefab") {
                        surfaceCount += MigrateWaterFile(entry.path(), profileValues);
                    }
                }
            }

            for (const KeyMove& move : kProfileMoves) {
                config.erase(move.legacy);
            }
            for (const KeyMove& move : kEnvironmentMoves) {
                config.erase(move.legacy);
            }
            if (!WriteJson(configPath, config)) {
                Logger::GetInstance().Errorf(LogCategory::System,
                    "WaterSettingsMigration: CVars.json を書けませんでした（控え: {}）",
                    Logger::GetInstance().PathToUtf8(backupPath));
                return;
            }

            Logger::GetInstance().Infof(LogCategory::System,
                "WaterSettingsMigration: CVars.json の水の性質 {} 件を水面 {} 個へ、風 {} 件をシーン {} 個へ移しました（元の CVars.json: {}）",
                profileValues.size(), surfaceCount, environmentValues.size(), environmentCount,
                Logger::GetInstance().PathToUtf8(backupPath));
        }
    }

    void MigrateProject()
    {
        try {
            Migrate();
        } catch (const std::exception& e) {
            Logger::GetInstance().Errorf(LogCategory::System,
                "WaterSettingsMigration: 水の設定を移している途中で失敗しました: {}", e.what());
        }
    }
}
