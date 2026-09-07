#include "pch.h"
#include "CameraShakePresetLibrary.h"

#include "Camera/Shake/CameraShakePresets.h"
#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief 列挙値を int で保存し、範囲外なら既定へ倒す
        template<typename Enum>
        Enum ToEnum(int value, int count, Enum fallback)
        {
            return (value >= 0 && value < count) ? static_cast<Enum>(value) : fallback;
        }

        constexpr int kWaveformCount = 4;   // Perlin / Random / Sine / Kick
        constexpr int kSpaceCount = 2;      // CameraLocal / World
        constexpr int kTimeModeCount = 2;   // Scaled / Unscaled
        constexpr int kEasingCount = 64;    // EasingUtil::Type の上限としては十分に広い

        json ParamsToJson(const CameraShakeParams& params)
        {
            json data;
            data["positionAmplitude"] = JsonManager::Vector3ToJson(params.positionAmplitude);
            data["rotationAmplitude"] = JsonManager::Vector3ToJson(params.rotationAmplitude);
            data["fovAmplitude"] = params.fovAmplitude;
            data["frequency"] = params.frequency;
            data["frequencyScale"] = JsonManager::Vector3ToJson(params.frequencyScale);
            data["duration"] = params.duration;
            data["attack"] = params.attack;
            data["decayEase"] = static_cast<int>(params.decayEase);
            data["direction"] = JsonManager::Vector3ToJson(params.direction);
            data["directionality"] = params.directionality;
            data["useWorldFalloff"] = params.useWorldFalloff;
            data["innerRadius"] = params.innerRadius;
            data["outerRadius"] = params.outerRadius;
            data["falloffEase"] = static_cast<int>(params.falloffEase);
            data["waveform"] = static_cast<int>(params.waveform);
            data["space"] = static_cast<int>(params.space);
            data["timeMode"] = static_cast<int>(params.timeMode);
            data["seed"] = params.seed;
            return data;
        }

        CameraShakeParams JsonToParams(const json& data)
        {
            // 欠けた項目は構造体の既定値のまま残す。項目を足しても古いファイルが読める。
            CameraShakeParams params{};
            params.positionAmplitude = JsonManager::SafeGetVector3(data, "positionAmplitude", params.positionAmplitude);
            params.rotationAmplitude = JsonManager::SafeGetVector3(data, "rotationAmplitude", params.rotationAmplitude);
            params.fovAmplitude = JsonManager::SafeGet(data, "fovAmplitude", params.fovAmplitude);
            params.frequency = JsonManager::SafeGet(data, "frequency", params.frequency);
            params.frequencyScale = JsonManager::SafeGetVector3(data, "frequencyScale", params.frequencyScale);
            params.duration = JsonManager::SafeGet(data, "duration", params.duration);
            params.attack = JsonManager::SafeGet(data, "attack", params.attack);
            params.decayEase = ToEnum(JsonManager::SafeGet(data, "decayEase", static_cast<int>(params.decayEase)),
                kEasingCount, params.decayEase);
            params.direction = JsonManager::SafeGetVector3(data, "direction", params.direction);
            params.directionality = JsonManager::SafeGet(data, "directionality", params.directionality);
            params.useWorldFalloff = JsonManager::SafeGet(data, "useWorldFalloff", params.useWorldFalloff);
            params.innerRadius = JsonManager::SafeGet(data, "innerRadius", params.innerRadius);
            params.outerRadius = JsonManager::SafeGet(data, "outerRadius", params.outerRadius);
            params.falloffEase = ToEnum(JsonManager::SafeGet(data, "falloffEase", static_cast<int>(params.falloffEase)),
                kEasingCount, params.falloffEase);
            params.waveform = ToEnum(JsonManager::SafeGet(data, "waveform", static_cast<int>(params.waveform)),
                kWaveformCount, params.waveform);
            params.space = ToEnum(JsonManager::SafeGet(data, "space", static_cast<int>(params.space)),
                kSpaceCount, params.space);
            params.timeMode = ToEnum(JsonManager::SafeGet(data, "timeMode", static_cast<int>(params.timeMode)),
                kTimeModeCount, params.timeMode);
            params.seed = JsonManager::SafeGet(data, "seed", params.seed);
            return params;
        }
    }

    CameraShakePresetLibrary::CameraShakePresetLibrary()
    {
        ResetToBuiltIn();
    }

    void CameraShakePresetLibrary::ResetToBuiltIn()
    {
        // 名前は「イベントトラックから呼ぶときの識別子」でもあるので、
        // ゲーム側で意味が通る素直な名前にしておく。
        presets_ = {
            { "Hit",        CameraShakePresets::Hit() },
            { "HeavyHit",   CameraShakePresets::HeavyHit() },
            { "Explosion",  CameraShakePresets::Explosion() },
            { "Landing",    CameraShakePresets::Landing() },
            { "Recoil",     CameraShakePresets::Recoil() },
            { "Earthquake", CameraShakePresets::Earthquake() },
            { "Handheld",   CameraShakePresets::Handheld() },
            { "Rumble",     CameraShakePresets::Rumble() },
        };
    }

    const CameraShakeParams* CameraShakePresetLibrary::Find(const std::string& name) const
    {
        const auto found = std::find_if(presets_.begin(), presets_.end(),
            [&name](const CameraShakePreset& preset) { return preset.name == name; });
        return (found != presets_.end()) ? &found->params : nullptr;
    }

    CameraShakeParams* CameraShakePresetLibrary::FindMutable(const std::string& name)
    {
        const auto found = std::find_if(presets_.begin(), presets_.end(),
            [&name](const CameraShakePreset& preset) { return preset.name == name; });
        return (found != presets_.end()) ? &found->params : nullptr;
    }

    void CameraShakePresetLibrary::Set(const std::string& name, const CameraShakeParams& params)
    {
        if (name.empty()) {
            return;
        }

        if (CameraShakeParams* existing = FindMutable(name)) {
            *existing = params;
            return;
        }

        presets_.push_back({ name, params });
    }

    void CameraShakePresetLibrary::Remove(const std::string& name)
    {
        presets_.erase(
            std::remove_if(presets_.begin(), presets_.end(),
                [&name](const CameraShakePreset& preset) { return preset.name == name; }),
            presets_.end());
    }

    bool CameraShakePresetLibrary::Rename(const std::string& from, const std::string& to)
    {
        if (to.empty() || from == to || Find(to) != nullptr) {
            return false;
        }

        const auto found = std::find_if(presets_.begin(), presets_.end(),
            [&from](const CameraShakePreset& preset) { return preset.name == from; });
        if (found == presets_.end()) {
            return false;
        }

        found->name = to;
        return true;
    }

    std::string CameraShakePresetLibrary::GetFilePath()
    {
        return (std::filesystem::path(kDirectory) / kFileName).string();
    }

    bool CameraShakePresetLibrary::Save() const
    {
        json root;
        root["version"] = "1.0";

        json presetsJson = json::array();
        for (const auto& preset : presets_) {
            json entry;
            entry["name"] = preset.name;
            entry["params"] = ParamsToJson(preset.params);
            presetsJson.push_back(entry);
        }
        root["presets"] = presetsJson;

        JsonManager::GetInstance().CreateJsonDirectory(kDirectory);
        return JsonManager::GetInstance().SaveJson(GetFilePath(), root);
    }

    bool CameraShakePresetLibrary::Load()
    {
        const std::string path = GetFilePath();
        if (!JsonManager::GetInstance().FileExists(path)) {
            return false;
        }

        const json root = JsonManager::GetInstance().LoadJson(path);
        if (root.empty() || !root.contains("presets") || !root["presets"].is_array()) {
            return false;
        }

        // 読み切れたときだけ差し替える。壊れたファイルで組み込みプリセットまで失わない。
        std::vector<CameraShakePreset> loaded;
        for (const auto& entry : root["presets"]) {
            CameraShakePreset preset{};
            preset.name = JsonManager::SafeGet(entry, "name", std::string());
            if (preset.name.empty()) {
                continue;
            }
            if (entry.contains("params")) {
                preset.params = JsonToParams(entry["params"]);
            }
            loaded.push_back(std::move(preset));
        }

        if (loaded.empty()) {
            return false;
        }

        presets_ = std::move(loaded);
        return true;
    }
}
