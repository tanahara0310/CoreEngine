#pragma once
#include <string>

namespace CoreEngine
{
    // アセットの種類
    enum class AssetType
    {
        Unknown,
        Texture,
        Model,
        Shader,
        Audio,
        Material,
        Scene,
        Prefab,
        Animation
    };

    // AssetType を文字列に変換
    inline std::string AssetTypeToString(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture:    return "Texture";
        case AssetType::Model:      return "Model";
        case AssetType::Shader:     return "Shader";
        case AssetType::Audio:      return "Audio";
        case AssetType::Material:   return "Material";
        case AssetType::Scene:      return "Scene";
        case AssetType::Prefab:     return "Prefab";
        case AssetType::Animation:  return "Animation";
        default:                    return "Unknown";
        }
    }

    // 文字列を AssetType に変換
    inline AssetType StringToAssetType(const std::string& typeStr)
    {
        if (typeStr == "Texture")   return AssetType::Texture;
        if (typeStr == "Model")     return AssetType::Model;
        if (typeStr == "Shader")    return AssetType::Shader;
        if (typeStr == "Audio")     return AssetType::Audio;
        if (typeStr == "Material")  return AssetType::Material;
        if (typeStr == "Scene")     return AssetType::Scene;
        if (typeStr == "Prefab")    return AssetType::Prefab;
        if (typeStr == "Animation") return AssetType::Animation;
        return AssetType::Unknown;
    }
}
