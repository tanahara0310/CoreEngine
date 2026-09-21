#include "pch.h"
#include "Collision/CollisionLayer.h"

#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kSettingsPath = "Application/Config/EngineSettings/Layers.json";
        constexpr const char* kLayersKey = "layers";
        constexpr const char* kVersionKey = "version";
        constexpr const char* kVersion = "1.0";

        /// @brief ファイルが無いときの並び
        std::vector<std::string> DefaultNames()
        {
            return {
                "Default", "Player", "Enemy", "PlayerBullet", "EnemyBullet",
                "Boss", "BossBullet", "BossAttack", "Item", "Environment",
            };
        }

        /// @brief 名前の並びとして成り立っているか
        bool Validate(const std::vector<std::string>& names, std::string* outError)
        {
            const auto fail = [outError](const char* reason) {
                if (outError) { *outError = reason; }
                return false;
                };

            if (names.empty()) {
                return fail("レイヤーが 1 つもありません");
            }
            if (names.size() > kMaxCollisionLayers) {
                return fail("レイヤーが多すぎます（32 まで）");
            }
            if (names.front() != "Default") {
                return fail("先頭は Default でなければなりません");
            }
            for (const std::string& name : names) {
                if (name.empty()) {
                    return fail("空の名前は使えません");
                }
            }
            std::vector<std::string> sorted = names;
            std::sort(sorted.begin(), sorted.end());
            if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
                return fail("同じ名前が 2 つあります");
            }
            if (outError) { outError->clear(); }
            return true;
        }

        std::vector<std::string>& Table()
        {
            static std::vector<std::string> names = [] {
                std::vector<std::string> loaded;
                const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
                std::ifstream in(path, std::ios::binary);
                if (in) {
                    const nlohmann::json root =
                        nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
                    if (!root.is_discarded() && root.is_object()) {
                        if (const auto found = root.find(kLayersKey);
                            found != root.end() && found->is_array()) {
                            for (const auto& entry : *found) {
                                if (entry.is_string()) {
                                    loaded.push_back(entry.get<std::string>());
                                }
                            }
                        }
                    }
                }
                // 壊れていたら既定へ倒す（ここはログの初期化より前に走りうる）
                return Validate(loaded, nullptr) ? loaded : DefaultNames();
            }();
            return names;
        }

        bool Write(const std::vector<std::string>& names)
        {
            const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return false;
            }

            nlohmann::json root;
            root[kVersionKey] = kVersion;
            root[kLayersKey] = names;

            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            out << root.dump(4) << '\n';
            return static_cast<bool>(out);
        }
    }

    std::size_t CollisionLayers::Count()
    {
        return Table().size();
    }

    const std::vector<std::string>& CollisionLayers::Names()
    {
        return Table();
    }

    bool CollisionLayers::SetNames(std::vector<std::string> names, std::string* outError)
    {
        if (!Validate(names, outError)) {
            return false;
        }
        if (!Write(names)) {
            if (outError) { *outError = "保存できませんでした"; }
            return false;
        }
        Table() = std::move(names);
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "当たり判定のレイヤーを {} 個にしました", Table().size());
        return true;
    }

    void CollisionLayers::Reload()
    {
        std::vector<std::string> loaded;
        const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
        std::ifstream in(path, std::ios::binary);
        if (in) {
            const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
            if (!root.is_discarded() && root.is_object()) {
                if (const auto found = root.find(kLayersKey);
                    found != root.end() && found->is_array()) {
                    for (const auto& entry : *found) {
                        if (entry.is_string()) {
                            loaded.push_back(entry.get<std::string>());
                        }
                    }
                }
            }
        }
        Table() = Validate(loaded, nullptr) ? std::move(loaded) : DefaultNames();
    }

    const std::string& ToString(CollisionLayer layer)
    {
        const std::vector<std::string>& names = Table();
        const auto index = static_cast<std::size_t>(layer);
        return index < names.size() ? names[index] : names.front();
    }

    bool TryParseCollisionLayer(std::string_view name, CollisionLayer& out)
    {
        const std::vector<std::string>& names = Table();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                out = static_cast<CollisionLayer>(i);
                return true;
            }
        }
        return false;
    }
}
