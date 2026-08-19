#include "pch.h"
#include "ShaderBlobCache.h"

namespace CoreEngine
{
    ShaderBlobCache& ShaderBlobCache::GetInstance()
    {
        static ShaderBlobCache instance;
        return instance;
    }

    std::wstring ShaderBlobCache::MakeKey(const std::wstring& resolvedPath,
        const std::wstring& profile,
        const std::wstring& entryPoint)
    {
        // 区切りに使う '|' はパスに出現しない文字なので、連結による衝突は起きない
        return resolvedPath + L"|" + profile + L"|" + entryPoint;
    }

    void ShaderBlobCache::Store(const std::wstring& resolvedPath,
        const std::wstring& profile,
        const std::wstring& entryPoint,
        const void* data,
        size_t size)
    {
        if (!enabled_ || !data || size == 0) {
            return;
        }

        const auto* bytes = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> copy(bytes, bytes + size);

        std::lock_guard<std::mutex> lock(mutex_);
        blobs_[MakeKey(resolvedPath, profile, entryPoint)] = std::move(copy);
    }

    bool ShaderBlobCache::TryGet(const std::wstring& resolvedPath,
        const std::wstring& profile,
        const std::wstring& entryPoint,
        std::vector<uint8_t>& blobOut) const
    {
        if (!enabled_) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = blobs_.find(MakeKey(resolvedPath, profile, entryPoint));
        if (it == blobs_.end()) {
            return false;
        }
        blobOut = it->second;
        return true;
    }

    size_t ShaderBlobCache::GetTotalBytes() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& [key, blob] : blobs_) {
            total += blob.size();
        }
        return total;
    }

    size_t ShaderBlobCache::GetCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return blobs_.size();
    }

    void ShaderBlobCache::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        blobs_.clear();
    }
}
