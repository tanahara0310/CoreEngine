#include "pch.h"
#include "CameraSequenceLibrary.h"

#include "Camera/Sequence/CameraSequenceIO.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kExtension = ".json";

        /// @brief 末尾の ".json" を取り除いた名前を返す
        std::string StripExtension(const std::string& name)
        {
            const std::string extension = kExtension;
            if (name.size() > extension.size()
                && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
                return name.substr(0, name.size() - extension.size());
            }
            return name;
        }
    }

    void CameraSequenceLibrary::SetDirectory(const std::string& directoryPath)
    {
        if (directoryPath_ == directoryPath) {
            return;
        }

        directoryPath_ = directoryPath;
        ClearCache();
    }

    std::string CameraSequenceLibrary::ResolvePath(const std::string& name) const
    {
        const std::filesystem::path path =
            std::filesystem::path(directoryPath_) / (StripExtension(name) + kExtension);
        return path.string();
    }

    std::shared_ptr<const CameraSequenceAsset> CameraSequenceLibrary::Get(const std::string& name)
    {
        if (name.empty()) {
            return nullptr;
        }

        const std::string key = StripExtension(name);

        // 失敗も含めて 1 度引いた結果は覚えている。毎フレーム Play を呼ばれても
        // 存在しないファイルを探し続けない。
        const auto found = cache_.find(key);
        if (found != cache_.end()) {
            return found->second;
        }

        auto asset = std::make_shared<CameraSequenceAsset>();
        if (!CameraSequenceIO::Load(ResolvePath(key), *asset)) {
            Logger::GetInstance().Errorf(
                LogCategory::Resource,
                "CameraSequenceLibrary: シーケンス '%s' を読み込めませんでした (%s)",
                key.c_str(), ResolvePath(key).c_str());
            cache_.emplace(key, nullptr);
            return nullptr;
        }

        auto stored = std::shared_ptr<const CameraSequenceAsset>(std::move(asset));
        cache_.emplace(key, stored);
        return stored;
    }

    void CameraSequenceLibrary::Reload(const std::string& name)
    {
        cache_.erase(StripExtension(name));
    }

    void CameraSequenceLibrary::ClearCache()
    {
        cache_.clear();
    }

    std::vector<std::string> CameraSequenceLibrary::ListNames() const
    {
        std::vector<std::string> names = CameraSequenceIO::GetSequenceFileList(directoryPath_);
        for (auto& name : names) {
            name = StripExtension(name);
        }
        return names;
    }
}
