#include "pch.h"
#include "EngineConfig.h"
#include "EngineSystem/Relaunch.h"
#include "Utility/JsonManager/JsonFields.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <Windows.h>
#include <iostream>

namespace CoreEngine
{
    // ビルド構成に応じたコンフィグファイルパス
#if defined(_DEBUG)
    static constexpr const char* kEngineConfigPath = "Engine/Config/config_debug.json";
#elif defined(NDEBUG)
    static constexpr const char* kEngineConfigPath = "Engine/Config/config_release.json";
#else
    static constexpr const char* kEngineConfigPath = "Engine/Config/config_develop.json";
#endif

    namespace
    {
        /// @brief 保存する項目を、入れる欄と保存キーと一緒に 1 つずつ渡す
        constexpr auto kConfigFields = [](auto& config, auto&& field) {
            field("window", "title", config.windowTitle);
            field("window", "width", config.windowWidth);
            field("window", "height", config.windowHeight);
            field("debug", "enableDebugLayer", config.enableDebugLayer);
            field("debug", "enableGPUBasedValidation", config.enableGPUBasedValidation);
            field("debug", "enableDRED", config.enableDRED);
            field("debug", "enablePixRuntime", config.enablePixRuntime);
            field("descriptors", "maxSRVDescriptors", config.maxSRVDescriptors);
            field("descriptors", "maxRTVDescriptors", config.maxRTVDescriptors);
            field("descriptors", "maxDSVDescriptors", config.maxDSVDescriptors);
            field("command", "frameCount", config.frameCount);
            field("shader", "enableCache", config.enableShaderCache);
            field("threads", "workerLimit", config.threadWorkerLimit);
        };
    }

    EngineConfig EngineConfig::Load()
    {
        EngineConfig config;

        auto& jsonManager = JsonManager::GetInstance();

        if (!jsonManager.FileExists(kEngineConfigPath)) {
            // コンフィグファイルが存在しない場合、デフォルト値で作成
            std::cerr << "Engine config not found: " << kEngineConfigPath
                << " — creating with defaults." << std::endl;

            json j;
            kConfigFields(config, [&j](const char* section, const char* key, const auto& value) {
                j[section][key] = JsonFields::ToJsonValue(value);
            });

            jsonManager.SaveJson(kEngineConfigPath, j);
            return config;
        }

        json j = jsonManager.LoadJson(kEngineConfigPath);

        // 欄と項目があるものだけ読む
        kConfigFields(config, [&j](const char* section, const char* key, auto& value) {
            if (j.contains(section)) {
                JsonFields::ReadValue(j[section], key, value, JsonFields::kIfPresent);
            }
        });

        return config;
    }

    std::wstring EngineConfig::GetWindowTitleWide() const
    {
        if (windowTitle.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, windowTitle.c_str(), -1, nullptr, 0);
        std::wstring result(size - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, windowTitle.c_str(), -1, result.data(), size);
        return result;
    }

    void EngineConfig::SetPixRuntimeAndRestart(bool enable)
    {
        auto& jsonManager = JsonManager::GetInstance();

        if (!jsonManager.FileExists(kEngineConfigPath)) {
            return;
        }

        // コンフィグの enablePixRuntime だけ書き換えて保存
        json j = jsonManager.LoadJson(kEngineConfigPath);
        j["debug"]["enablePixRuntime"] = enable;
        jsonManager.SaveJson(kEngineConfigPath, j);

        // 終わってから起動し直す（プロジェクトを指定して開いていれば、同じプロジェクトを開き直す）
        if (ProjectPaths::IsProjectSpecified()) {
            Relaunch::RequestProject(ProjectPaths::ProjectRoot());
        } else {
            Relaunch::Request({});
        }
    }

}
