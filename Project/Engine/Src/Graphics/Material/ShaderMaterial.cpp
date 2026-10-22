#include "pch.h"
#include "ShaderMaterial.h"

#include "Graphics/Asset/AssetRef.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kShaderKey = "shader";
        constexpr const char* kVertexShaderKey = "vertexShader";
        constexpr const char* kValuesKey = "values";
        constexpr const char* kTexturesKey = "textures";
        constexpr const char* kLinearKey = "linear";

        /// @brief 1 つの値に持てる数の上限（float4）
        constexpr size_t kMaxComponents = 4;

        /// @brief 数の配列を読む（数でない要素があれば false）
        bool ReadNumbers(const json& node, MaterialNumbers& out)
        {
            out.clear();
            if (node.is_number()) {
                out.push_back(node.get<float>());
                return true;
            }
            if (!node.is_array() || node.empty() || node.size() > kMaxComponents) {
                return false;
            }
            for (const json& element : node) {
                if (!element.is_number()) {
                    return false;
                }
                out.push_back(element.get<float>());
            }
            return true;
        }

        /// @brief 数を JSON にする（1 個なら数、2 個以上なら配列）
        json WriteNumbers(const MaterialNumbers& numbers)
        {
            if (numbers.size() == 1) {
                return numbers.front();
            }
            json array = json::array();
            for (const float number : numbers) {
                array.push_back(number);
            }
            return array;
        }

        /// @brief ファイルの更新時刻（読めなければ既定値）
        std::filesystem::file_time_type WriteTimeOf(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            return error ? std::filesystem::file_time_type{} : time;
        }
    }

    TextureManager::LoadedTexture LoadMaterialTexture(const MaterialTexture& texture)
    {
        TextureManager& textures = TextureManager::GetInstance();
        if (texture.path.empty() || !textures.IsInitialized()) {
            return {};
        }
        return textures.Load(texture.path, texture.linear ? TextureColorSpace::Linear : TextureColorSpace::SRGB);
    }

    bool SplitMaterialElementName(std::string_view key, std::string_view& name, uint32_t& index)
    {
        name = key;
        index = 0;
        const size_t open = key.find('[');
        if (open == std::string_view::npos || key.empty() || key.back() != ']' || open == 0) {
            return false;
        }
        const std::string_view digits = key.substr(open + 1, key.size() - open - 2);
        uint32_t parsed = 0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        if (digits.empty() || result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
            return false;
        }
        name = key.substr(0, open);
        index = parsed;
        return true;
    }

    void ShaderMaterial::Clear()
    {
        shaderPath_.clear();
        vertexShaderPath_.clear();
        values_.clear();
        textures_.clear();
        loadedTextures_.clear();
        ++revision_;
    }

    std::filesystem::path ShaderMaterial::GetFullPath() const
    {
        return ProjectPaths::Resolve(assetPath_);
    }

    bool ShaderMaterial::Load(const std::string& assetPath)
    {
        assetPath_ = assetPath;
        Clear();

        const std::filesystem::path fullPath = GetFullPath();
        loadedWriteTime_ = WriteTimeOf(fullPath);

        std::ifstream in(fullPath, std::ios::binary);
        if (!in) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "マテリアル {} を開けません", assetPath_);
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();

        const json root = json::parse(text.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "マテリアル {} は JSON として読めません", assetPath_);
            return false;
        }

        const std::string context = "マテリアル " + assetPath_;
        if (const auto shader = root.find(kShaderKey); shader != root.end()) {
            shaderPath_ = JsonToAssetPath(*shader, context);
        }
        if (const auto vertexShader = root.find(kVertexShaderKey); vertexShader != root.end()) {
            vertexShaderPath_ = JsonToAssetPath(*vertexShader, context);
        }

        if (const auto values = root.find(kValuesKey); values != root.end() && values->is_object()) {
            for (const auto& [name, node] : values->items()) {
                MaterialNumbers numbers;
                if (ReadNumbers(node, numbers)) {
                    values_[name] = std::move(numbers);
                    continue;
                }
                // 配列の項目は要素ごとの数の配列を並べて書く
                bool isElementList = node.is_array() && !node.empty();
                if (isElementList) {
                    for (size_t index = 0; index < node.size(); ++index) {
                        if (!ReadNumbers(node[index], numbers)) {
                            isElementList = false;
                            break;
                        }
                        values_[name + "[" + std::to_string(index) + "]"] = numbers;
                    }
                }
                if (!isElementList) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "{} の値 {} は数か 4 個までの数の配列で書きます（読み飛ばしました）", context, name);
                }
            }
        }

        if (const auto textures = root.find(kTexturesKey); textures != root.end() && textures->is_object()) {
            for (const auto& [name, node] : textures->items()) {
                MaterialTexture texture;
                texture.path = JsonToAssetPath(node, context);
                if (node.is_object()) {
                    texture.linear = node.value(kLinearKey, false);
                }
                if (!texture.path.empty()) {
                    loadedTextures_[name] = LoadMaterialTexture(texture);
                    textures_[name] = std::move(texture);
                }
            }
        }

        ++revision_;
        return true;
    }

    bool ShaderMaterial::Save()
    {
        json root = json::object();
        root[kShaderKey] = AssetPathToJson(shaderPath_);
        if (!vertexShaderPath_.empty()) {
            root[kVertexShaderKey] = AssetPathToJson(vertexShaderPath_);
        }

        // 配列の要素（名前[番号]）は、要素の数の配列を番号の順に並べた 1 つの項目へまとめる
        json values = json::object();
        std::map<std::string, std::map<uint32_t, const MaterialNumbers*>> elements;
        for (const auto& [key, numbers] : values_) {
            std::string_view name;
            uint32_t index = 0;
            if (SplitMaterialElementName(key, name, index)) {
                elements[std::string(name)][index] = &numbers;
            } else {
                values[key] = WriteNumbers(numbers);
            }
        }
        for (const auto& [name, byIndex] : elements) {
            const uint32_t count = byIndex.rbegin()->first + 1;
            json list = json::array();
            for (uint32_t index = 0; index < count; ++index) {
                const auto found = byIndex.find(index);
                json element = json::array();
                if (found != byIndex.end()) {
                    for (const float number : *found->second) {
                        element.push_back(number);
                    }
                } else {
                    element.push_back(0.0f);
                }
                list.push_back(std::move(element));
            }
            values[name] = std::move(list);
        }
        root[kValuesKey] = std::move(values);

        json textures = json::object();
        for (const auto& [name, texture] : textures_) {
            json node = AssetPathToJson(texture.path);
            if (texture.linear && node.is_object()) {
                node[kLinearKey] = true;
            }
            textures[name] = std::move(node);
        }
        root[kTexturesKey] = std::move(textures);

        const std::filesystem::path fullPath = GetFullPath();
        std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "マテリアル {} を書けません", assetPath_);
            return false;
        }
        out << root.dump(4) << "\n";
        out.close();
        if (!out) {
            return false;
        }
        loadedWriteTime_ = WriteTimeOf(fullPath);
        return true;
    }

    bool ShaderMaterial::ReadShaderPaths(const std::filesystem::path& fullPath, std::string& shaderPath,
        std::string& vertexShaderPath)
    {
        shaderPath.clear();
        vertexShaderPath.clear();
        std::ifstream in(fullPath, std::ios::binary);
        if (!in) {
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();
        const json root = json::parse(text.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }
        const std::string context = "マテリアル " + Logger::GetInstance().PathToUtf8(fullPath);
        if (const auto shader = root.find(kShaderKey); shader != root.end()) {
            shaderPath = JsonToAssetPath(*shader, context);
        }
        if (const auto vertexShader = root.find(kVertexShaderKey); vertexShader != root.end()) {
            vertexShaderPath = JsonToAssetPath(*vertexShader, context);
        }
        return true;
    }

    bool ShaderMaterial::ReadTextures(const std::filesystem::path& fullPath, std::vector<MaterialTexture>& textures)
    {
        textures.clear();
        std::ifstream in(fullPath, std::ios::binary);
        if (!in) {
            return false;
        }
        std::stringstream text;
        text << in.rdbuf();
        const json root = json::parse(text.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return false;
        }
        const std::string context = "マテリアル " + Logger::GetInstance().PathToUtf8(fullPath);
        if (const auto nodes = root.find(kTexturesKey); nodes != root.end() && nodes->is_object()) {
            for (const auto& [name, node] : nodes->items()) {
                MaterialTexture texture;
                texture.path = JsonToAssetPath(node, context);
                if (node.is_object()) {
                    texture.linear = node.value(kLinearKey, false);
                }
                if (!texture.path.empty()) {
                    textures.push_back(std::move(texture));
                }
            }
        }
        return true;
    }

    bool ShaderMaterial::Revert()
    {
        const std::string assetPath = assetPath_;
        return Load(assetPath);
    }

    void ShaderMaterial::SetShaderPath(std::string path)
    {
        if (shaderPath_ != path) {
            shaderPath_ = std::move(path);
            ++revision_;
        }
    }

    void ShaderMaterial::SetVertexShaderPath(std::string path)
    {
        if (vertexShaderPath_ != path) {
            vertexShaderPath_ = std::move(path);
            ++revision_;
        }
    }

    void ShaderMaterial::SetNumbers(std::string_view name, std::span<const float> numbers)
    {
        if (name.empty() || numbers.empty()) {
            return;
        }
        const size_t count = (std::min)(numbers.size(), kMaxComponents);
        MaterialNumbers value(numbers.begin(), numbers.begin() + static_cast<std::ptrdiff_t>(count));

        const auto found = values_.find(name);
        if (found != values_.end()) {
            if (found->second == value) {
                return;
            }
            found->second = std::move(value);
        } else {
            values_.emplace(std::string(name), std::move(value));
        }
        ++revision_;
    }

    const MaterialNumbers* ShaderMaterial::FindNumbers(std::string_view name) const
    {
        const auto found = values_.find(name);
        return found != values_.end() ? &found->second : nullptr;
    }

    void ShaderMaterial::RemoveNumbers(std::string_view name)
    {
        const auto found = values_.find(name);
        if (found != values_.end()) {
            values_.erase(found);
            ++revision_;
        }
    }

    void ShaderMaterial::SetTexture(std::string_view name, const MaterialTexture& texture)
    {
        if (name.empty()) {
            return;
        }
        const auto found = textures_.find(name);
        if (texture.path.empty()) {
            if (found != textures_.end()) {
                textures_.erase(found);
                if (const auto loaded = loadedTextures_.find(name); loaded != loadedTextures_.end()) {
                    loadedTextures_.erase(loaded);
                }
                ++revision_;
            }
            return;
        }
        if (found != textures_.end()) {
            if (found->second == texture) {
                return;
            }
            found->second = texture;
        } else {
            textures_.emplace(std::string(name), texture);
        }
        loadedTextures_[std::string(name)] = LoadMaterialTexture(texture);
        ++revision_;
    }

    const MaterialTexture* ShaderMaterial::FindTexture(std::string_view name) const
    {
        const auto found = textures_.find(name);
        return found != textures_.end() ? &found->second : nullptr;
    }

    const TextureManager::LoadedTexture* ShaderMaterial::FindLoadedTexture(std::string_view name) const
    {
        const auto found = loadedTextures_.find(name);
        return found != loadedTextures_.end() ? &found->second : nullptr;
    }
}
