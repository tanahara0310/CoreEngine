#include "pch.h"
#include "Text/FontManager.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        // ── FNV-1a 64bit ────────────────────────────────────────
        constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
        constexpr uint64_t kFnvPrime = 1099511628211ULL;

        void HashBytes(uint64_t& hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= kFnvPrime;
            }
        }

        template <class T>
        void HashValue(uint64_t& hash, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            HashBytes(hash, &value, sizeof(T));
        }
    } // namespace

    FontManager::~FontManager()
    {
        Finalize();
    }

    void FontManager::Initialize(GraphicsCore* graphicsCore)
    {
        graphicsCore_ = graphicsCore;

        // 実行時グリフのベイク用。バッチ内のグリフを並列に焼く
        bakeThreadPool_ = std::make_unique<ThreadPool>(kBakeThreadCount);
    }

    void FontManager::Finalize()
    {
        std::lock_guard lock(mutex_);
        // フォントのデストラクタが自分のベイクタスクを待つので、
        // プールを畳むのはフォントを全て捨ててから
        fonts_.clear();
        bakeThreadPool_.reset();
        graphicsCore_ = nullptr;
    }

    uint64_t FontManager::ComputeRequestHash(const MsdfFontDesc& desc)
    {
        uint64_t hash = kFnvOffsetBasis;

        HashBytes(hash, desc.filePath.data(), desc.filePath.size() * sizeof(wchar_t));
        HashValue(hash, desc.faceIndex);

        for (const std::wstring& name : desc.systemFamilyNames) {
            HashBytes(hash, name.data(), name.size() * sizeof(wchar_t));
            HashValue(hash, '|');
        }

        HashBytes(hash, desc.charsetUtf8.data(), desc.charsetUtf8.size());
        HashValue(hash, desc.includeAscii);
        HashValue(hash, desc.bake.glyphPixelSize);
        HashValue(hash, desc.bake.pxRange);
        HashValue(hash, desc.bake.atlasWidth);
        HashValue(hash, desc.bake.atlasHeight);
        HashValue(hash, desc.bake.padding);

        return hash;
    }

    MsdfFont* FontManager::Acquire(const MsdfFontDesc& desc)
    {
        if (!graphicsCore_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "FontManager: 初期化されていません");
            return nullptr;
        }

        const uint64_t key = ComputeRequestHash(desc);

        std::lock_guard lock(mutex_);

        // 同じ要求なら構築済みのものを返す。
        // 失敗したフォントも残しておき、毎シーン焼き直しに行かないようにする
        if (const auto it = fonts_.find(key); it != fonts_.end()) {
            MsdfFont* font = it->second.get();
            return font->IsValid() ? font : nullptr;
        }

        auto font = std::make_unique<MsdfFont>();
        const bool built = font->Build(graphicsCore_, bakeThreadPool_.get(), desc);

        MsdfFont* result = font.get();
        fonts_.emplace(key, std::move(font));

        if (!built) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "FontManager: フォントの構築に失敗しました");
            return nullptr;
        }

        return result;
    }

    size_t FontManager::GetFontCount() const
    {
        std::lock_guard lock(mutex_);
        return fonts_.size();
    }
}
