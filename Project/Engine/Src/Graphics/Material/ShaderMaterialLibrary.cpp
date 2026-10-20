#include "pch.h"
#include "ShaderMaterialLibrary.h"

#include "EngineSystem/PlaybackState.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <system_error>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// @brief ファイルの更新を見る間隔（フレーム）
        constexpr uint64_t kCheckIntervalFrames = 30;

        /// @brief ファイルの更新時刻（読めなければ既定値）
        std::filesystem::file_time_type WriteTimeOf(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            return error ? std::filesystem::file_time_type{} : time;
        }
    }

    ShaderMaterialLibrary& ShaderMaterialLibrary::GetInstance()
    {
        static ShaderMaterialLibrary instance;
        return instance;
    }

    std::shared_ptr<ShaderMaterial> ShaderMaterialLibrary::Get(std::string_view pathOrName)
    {
        if (pathOrName.empty()) {
            return nullptr;
        }

        // 綴りがそろうよう、アセットデータベースで引けたらその今のパスで持つ
        std::string assetPath(pathOrName);
        if (const AssetInfo* const info = FindAssetInfo(pathOrName, AssetType::Material)) {
            assetPath = ToAssetPath(*info);
        }

        std::lock_guard lock(mutex_);
        if (const auto found = materials_.find(assetPath); found != materials_.end()) {
            return found->second;
        }

        auto material = std::make_shared<ShaderMaterial>();
        if (!material->Load(assetPath)) {
            return nullptr;
        }
        materials_.emplace(assetPath, material);
        return material;
    }

    void ShaderMaterialLibrary::BeginFrame()
    {
        // 再生を止めたら、再生中に変えた値を捨てる
        const bool inPlayMode = PlaybackStateManager::GetInstance().IsInPlayMode();
        if (wasInPlayMode_ && !inPlayMode) {
            std::lock_guard lock(mutex_);
            for (auto& [path, material] : materials_) {
                material->Revert();
            }
        }
        wasInPlayMode_ = inPlayMode;

#ifdef CORE_EDITOR
        if (framesUntilCheck_ == 0) {
            framesUntilCheck_ = kCheckIntervalFrames;
            CheckFileChanges();
        }
        --framesUntilCheck_;
#endif
    }

    void ShaderMaterialLibrary::CheckFileChanges()
    {
        std::lock_guard lock(mutex_);

        std::vector<std::string> shaderPaths;
        for (auto& [path, material] : materials_) {
            if (WriteTimeOf(material->GetFullPath()) != material->GetLoadedWriteTime()) {
                material->Revert();
            }
            shaderPaths.push_back(material->GetShaderPath());
            shaderPaths.push_back(material->GetVertexShaderPath());
        }

        bool changed = false;
        for (const std::string& shaderPath : shaderPaths) {
            if (shaderPath.empty()) {
                continue;
            }
            const std::filesystem::path fullPath = ProjectPaths::Resolve(shaderPath);
            const auto time = WriteTimeOf(fullPath);
            const std::string key = Logger::GetInstance().PathToUtf8(fullPath);
            const auto found = shaderWriteTimes_.find(key);
            if (found == shaderWriteTimes_.end()) {
                shaderWriteTimes_.emplace(key, time);
            } else if (found->second != time) {
                found->second = time;
                changed = true;
            }
        }
        if (changed) {
            ++shaderRevision_;
        }
    }

    void ShaderMaterialLibrary::Clear()
    {
        std::lock_guard lock(mutex_);
        materials_.clear();
        shaderWriteTimes_.clear();
        ++shaderRevision_;
    }
}
