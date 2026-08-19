#include "pch.h"
#include "EngineConfig.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <Windows.h>
#include <shellapi.h>
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

    EngineConfig EngineConfig::Load()
    {
        EngineConfig config;

        auto& jsonManager = JsonManager::GetInstance();

        if (!jsonManager.FileExists(kEngineConfigPath)) {
            // コンフィグファイルが存在しない場合、デフォルト値で作成
            std::cerr << "Engine config not found: " << kEngineConfigPath
                << " — creating with defaults." << std::endl;

            json j;
            j["window"]["title"] = config.windowTitle;
            j["window"]["width"] = config.windowWidth;
            j["window"]["height"] = config.windowHeight;
            j["debug"]["enableDebugLayer"] = config.enableDebugLayer;
            j["debug"]["enableGPUBasedValidation"] = config.enableGPUBasedValidation;
            j["debug"]["enablePixRuntime"] = config.enablePixRuntime;
            j["shader"]["enableCache"] = config.enableShaderCache;
            j["descriptors"]["maxSRVDescriptors"] = config.maxSRVDescriptors;
            j["descriptors"]["maxRTVDescriptors"] = config.maxRTVDescriptors;
            j["descriptors"]["maxDSVDescriptors"] = config.maxDSVDescriptors;

            jsonManager.SaveJson(kEngineConfigPath, j);
            return config;
        }

        json j = jsonManager.LoadJson(kEngineConfigPath);

        // ウィンドウ設定
        if (j.contains("window")) {
            auto& w = j["window"];
            if (w.contains("title"))  config.windowTitle = w["title"].get<std::string>();
            if (w.contains("width"))  config.windowWidth = w["width"].get<int32_t>();
            if (w.contains("height")) config.windowHeight = w["height"].get<int32_t>();
        }

        // デバッグ設定
        if (j.contains("debug")) {
            auto& d = j["debug"];
            if (d.contains("enableDebugLayer"))          config.enableDebugLayer = d["enableDebugLayer"].get<bool>();
            if (d.contains("enableGPUBasedValidation"))  config.enableGPUBasedValidation = d["enableGPUBasedValidation"].get<bool>();
            if (d.contains("enablePixRuntime"))           config.enablePixRuntime = d["enablePixRuntime"].get<bool>();
        }

        // ディスクリプタ設定
        if (j.contains("descriptors")) {
            auto& desc = j["descriptors"];
            if (desc.contains("maxSRVDescriptors")) config.maxSRVDescriptors = desc["maxSRVDescriptors"].get<uint32_t>();
            if (desc.contains("maxRTVDescriptors")) config.maxRTVDescriptors = desc["maxRTVDescriptors"].get<uint32_t>();
            if (desc.contains("maxDSVDescriptors")) config.maxDSVDescriptors = desc["maxDSVDescriptors"].get<uint32_t>();
        }

        // シェーダ設定
        if (j.contains("shader")) {
            auto& s = j["shader"];
            if (s.contains("enableCache")) config.enableShaderCache = s["enableCache"].get<bool>();
        }

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

        // 現在の実行ファイルのフルパスを取得して新しいインスタンスを起動
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOWDEFAULT);

        // 現在のインスタンスを終了（次フレームの ProcessMessage で WM_QUIT を受け取り正常終了）
        PostQuitMessage(0);
    }

}
