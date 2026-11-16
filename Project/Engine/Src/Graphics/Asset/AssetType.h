#pragma once
#include <string>

#include "Utility/Enum/EnumNames.h"

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
        Animation,
        MaterialLibrary,
        Json,
        Csv,
        PhysicsMaterial,
        WaterProfile,
        ShaderGraph
    };

    // AssetType と保存に使う名前の表（Unknown は載せない）
    inline const auto& AssetTypeNames()
    {
        static constexpr EnumName<AssetType> kNames[] = {
            { AssetType::Texture,         "Texture" },
            { AssetType::Model,           "Model" },
            { AssetType::Shader,          "Shader" },
            { AssetType::Audio,           "Audio" },
            { AssetType::Material,        "Material" },
            { AssetType::Scene,           "Scene" },
            { AssetType::Prefab,          "Prefab" },
            { AssetType::Animation,       "Animation" },
            { AssetType::MaterialLibrary, "MaterialLibrary" },
            { AssetType::Json,            "Json" },
            { AssetType::Csv,             "Csv" },
            { AssetType::PhysicsMaterial, "PhysicsMaterial" },
            { AssetType::WaterProfile,    "WaterProfile" },
            { AssetType::ShaderGraph,     "ShaderGraph" },
        };
        return kNames;
    }

    // AssetType を文字列に変換
    inline std::string AssetTypeToString(AssetType type)
    {
        return std::string(NameOf(AssetTypeNames(), type, "Unknown"));
    }

    // 文字列を AssetType に変換
    inline AssetType StringToAssetType(const std::string& typeStr)
    {
        return ParseName(AssetTypeNames(), typeStr, AssetType::Unknown);
    }
}
