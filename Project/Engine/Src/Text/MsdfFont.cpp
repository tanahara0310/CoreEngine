#include "pch.h"
#include "Text/MsdfFont.h"

#include "Text/MsdfFontCache.h"
#include "Text/TextEncoding.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstring>

namespace CoreEngine
{
    namespace
    {
        /// @brief 値を alignment の倍数へ切り上げる
        constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        /// 1 回のベイクで処理する上限。多量の新規文字が一度に来ても、
        /// 定期的にアトラスへ反映して画面が徐々に埋まるようにする
        constexpr size_t kMaxGlyphsPerBakeBatch = 64;

        /// この件数だけ未保存が溜まったらキャッシュへ書き戻す
        /// @note 毎バッチ書くとアトラス全面（数 MB）の書き込みが頻発する
        constexpr uint32_t kCacheWriteBackThreshold = 32;
    } // namespace

    MsdfFont::~MsdfFont()
    {
        // ワーカーが this を触っている間に破棄されないよう、必ず先に待つ
        if (bakeTask_.valid()) {
            bakeTask_.wait();
        }

        // 実行時に足したぶんを残す。次回起動で □ が出る時間が消える
        if (writeBackEnabled_ && unsavedGlyphCount_.load(std::memory_order_relaxed) > 0) {
            SaveCacheSnapshot();
        }

        if (graphicsCore_ && atlasHandle_.IsValid()) {
            if (auto* allocator = graphicsCore_->GetDescriptorAllocator()) {
                allocator->Free(atlasHandle_);
            }
        }
    }

    bool MsdfFont::Build(GraphicsCore* graphicsCore, ThreadPool* threadPool, const MsdfFontDesc& desc)
    {
        if (!graphicsCore) { return false; }
        graphicsCore_ = graphicsCore;
        threadPool_ = threadPool;

        // ── ①フォールバック列を開く ────────────────────────────
        // 文字ごとに先頭から探すので、開けたものは全て残す。
        // 実行時ベイクでも使うため、フォントは Build 後も生かしておく
        fontChainNames_.clear();
        faces_.clear();

        if (!desc.filePath.empty()) {
            auto face = std::make_unique<DirectWriteFontFace>();
            if (face->LoadFromFile(desc.filePath, desc.faceIndex)) {
                fontChainNames_.push_back(face->GetDisplayName());
                faces_.push_back(std::move(face));
            }
        }

        for (const std::wstring& familyName : desc.systemFamilyNames) {
            auto face = std::make_unique<DirectWriteFontFace>();
            if (face->LoadFromSystem(familyName)) {
                fontChainNames_.push_back(familyName);
                faces_.push_back(std::move(face));
            }
        }

        if (faces_.empty()) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfFont: 指定されたフォントを 1 つも開けませんでした");
            return false;
        }

        resolvedFontName_ = fontChainNames_.front();

        faceChain_.clear();
        faceChain_.reserve(faces_.size());
        for (const auto& face : faces_) {
            faceChain_.push_back(face.get());
        }

        // ── ②起動時に焼いておく文字を集める ──────────────────────
        std::vector<char32_t> codePoints = Utf8ToUtf32(desc.charsetUtf8);
        if (desc.includeAscii) {
            for (char32_t cp = U' '; cp <= U'~'; ++cp) {
                codePoints.push_back(cp);
            }
        }
        // 改行はグリフを持たないのでアトラスへ入れない（レイアウト側で処理する）
        std::erase_if(codePoints, [](char32_t cp) {
            return cp == U'\n' || cp == U'\r' || cp == U'\t';
            });

        // ── ③アトラスを用意する（キャッシュ優先）────────────────
        MsdfBakeResult bake{};
        bool fromCache = false;

        if (desc.useDiskCache) {
            const uint64_t cacheKey = MsdfFontCache::ComputeKey(desc, fontChainNames_);
            cachePath_ = MsdfFontCache::MakePath(desc.cacheDirectory, cacheKey);
            fromCache = MsdfFontCache::TryLoad(cachePath_, bake);
        }

        if (fromCache) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                "MSDF アトラスをキャッシュから読み込みました: {} ({} グリフ / {} 枚)",
                Logger::GetInstance().PathToUtf8(cachePath_), bake.glyphs.size(), bake.pageCount);
            // メトリクスはフォント側の値を正とする（キャッシュにも入っているが同じ）
            bake.metrics = faceChain_.front()->GetMetrics();
        } else {
            bake = MsdfFontBaker::Bake(faceChain_, codePoints, desc.bake);
            if (!bake.success) {
                return false;
            }
            if (desc.useDiskCache && MsdfFontCache::Save(cachePath_, bake)) {
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                    "MSDF アトラスをキャッシュへ保存しました: {}",
                    Logger::GetInstance().PathToUtf8(cachePath_));
            }
        }

        // 目視確認用の PNG は、焼き直したときと出力が消えているときだけ書く
        if (!desc.debugAtlasDumpPath.empty()) {
            std::error_code ec;
            if (!fromCache || !std::filesystem::exists(desc.debugAtlasDumpPath, ec)) {
                MsdfFontBaker::SaveAtlasPng(bake, 0, desc.debugAtlasDumpPath);
            }
        }

        glyphs_ = bake.glyphs;
        notdefGlyph_ = bake.notdefGlyph;
        metrics_ = bake.metrics;
        bakeSettings_ = desc.bake;
        // キャッシュ側の枚数を正とする（設定を減らしても手持ちの画素は保つ）
        bakeSettings_.atlasPageCount =
            (std::max)(desc.bake.atlasPageCount, bake.pageCount);
        pxRange_ = desc.bake.pxRange;
        atlasSize_ = {
            static_cast<float>(bake.atlasWidth),
            static_cast<float>(bake.atlasHeight)
        };

        // 実行時の追加は、事前ベイクが使い終わった棚の続きから切り出す
        allocator_.Initialize(bake.atlasWidth, bake.atlasHeight,
            bakeSettings_.atlasPageCount, desc.bake.padding);
        allocator_.Restore(bake.allocatorState);
        atlasOccupancy_.store(allocator_.GetOccupancy(), std::memory_order_relaxed);
        usedPageCount_.store(allocator_.GetUsedPageCount(), std::memory_order_relaxed);

        // 既に焼いてある文字は要求済み扱いにしておく
        requestedCodePoints_.reserve(glyphs_.size());
        for (const auto& [codePoint, glyph] : glyphs_) {
            requestedCodePoints_.insert(codePoint);
        }

        atlasMirror_ = bake.pixels;
        mirrorPageCount_ = bake.pageCount;

        // ── ④GPU へ転送 ────────────────────────────────────────
        if (!CreateAtlasTexture(bake.atlasWidth, bake.atlasHeight, bakeSettings_.atlasPageCount)) {
            return false;
        }

        {
            // 焼けている枚だけ全面転送する（未使用の枚はゼロのままでよい）
            const size_t pageBytes =
                static_cast<size_t>(bake.atlasWidth) * bake.atlasHeight * 4;
            std::vector<AtlasUploadRegion> regions;
            regions.reserve(bake.pageCount);
            for (int page = 0; page < bake.pageCount; ++page) {
                regions.push_back(AtlasUploadRegion{
                    static_cast<uint32_t>(page), 0, 0, bake.atlasWidth, bake.atlasHeight,
                    bake.pixels.data() + pageBytes * page });
            }
            UploadRegions(regions);
        }

        dynamicGlyphsEnabled_ = desc.enableDynamicGlyphs && (threadPool_ != nullptr);
        writeBackEnabled_ = desc.useDiskCache && desc.writeBackRuntimeGlyphs
            && !cachePath_.empty();

        if (desc.enableDynamicGlyphs && !threadPool_) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "MsdfFont: ワーカーが渡されていないため動的グリフを無効にしました");
        }

        return true;
    }

    bool MsdfFont::CreateAtlasTexture(int width, int height, int pageCount)
    {
        auto* device = graphicsCore_->GetDevice();
        if (!device) { return false; }

        // ミップ 1 枚・非圧縮・R8G8B8A8_UNORM（＝リニア）で固定する。
        // _SRGB にするとサンプル時にガンマ変換が入り、距離値が歪んで輪郭がずれる。
        // ミップを持たせないのは、距離場の平均化がコーナーの median を壊すため。
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Width = static_cast<UINT64>(width);
        resourceDesc.Height = static_cast<UINT>(height);
        resourceDesc.DepthOrArraySize = static_cast<UINT16>(pageCount);
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        auto texture = ResourceFactory::CreateTextureResource(
            device, resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!texture) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfFont: アトラステクスチャの生成に失敗しました ({}x{} x{}枚)",
                width, height, pageCount);
            return false;
        }
        atlas_.Reset(texture, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = static_cast<UINT>(pageCount);
        srvDesc.Texture2DArray.PlaneSlice = 0;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

        auto* descriptorAllocator = graphicsCore_->GetDescriptorAllocator();
        if (!descriptorAllocator) { return false; }
        atlasHandle_ = descriptorAllocator->CreateSRV(atlas_.Get(), srvDesc, "MsdfFontAtlas");

        return atlasHandle_.gpuHandle.ptr != 0;
    }

    void MsdfFont::UploadRegions(const std::vector<AtlasUploadRegion>& regions)
    {
        if (regions.empty()) { return; }

        auto* device = graphicsCore_->GetDevice();
        auto* uploadContext = graphicsCore_->GetUploadContext();
        if (!device || !uploadContext || !atlas_.IsValid()) { return; }

        // 全領域を 1 本の UPLOAD バッファへ詰める。
        // テクスチャコピーの制約で、行ピッチは 256B・各領域の先頭は 512B 境界
        struct Placement { uint64_t offset; uint32_t rowPitch; };
        std::vector<Placement> placements(regions.size());

        uint64_t totalSize = 0;
        for (size_t i = 0; i < regions.size(); ++i) {
            const uint32_t rowPitch = static_cast<uint32_t>(
                AlignUp(static_cast<uint64_t>(regions[i].width) * 4,
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
            const uint64_t offset = AlignUp(totalSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

            placements[i] = { offset, rowPitch };
            totalSize = offset + static_cast<uint64_t>(rowPitch) * regions[i].height;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> staging =
            ResourceFactory::CreateBufferResource(device, static_cast<size_t>(totalSize));
        if (!staging) { return; }

        uint8_t* mapped = nullptr;
        if (FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
            return;
        }
        for (size_t i = 0; i < regions.size(); ++i) {
            const AtlasUploadRegion& region = regions[i];
            for (int y = 0; y < region.height; ++y) {
                std::memcpy(
                    mapped + placements[i].offset + static_cast<uint64_t>(y) * placements[i].rowPitch,
                    region.pixels + static_cast<size_t>(y) * region.width * 4,
                    static_cast<size_t>(region.width) * 4);
            }
        }
        staging->Unmap(0, nullptr);

        // 描画用コマンドリストではなく専用コンテキストへ積む。
        // UploadContext は描画と同じキューへ submit するので、
        // 直前フレームのアトラス参照とこのコピーはキュー上で直列化される
        UploadContext::ScopedRecording recording = uploadContext->BeginRecording();
        if (!recording.IsValid()) { return; }

        Barrier::Transition(recording.List(), atlas_, D3D12_RESOURCE_STATE_COPY_DEST);

        for (size_t i = 0; i < regions.size(); ++i) {
            const AtlasUploadRegion& region = regions[i];

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = atlas_.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            // ミップ 1 枚なので、配列の添字がそのままサブリソース番号になる
            dst.SubresourceIndex = region.page;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = staging.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = placements[i].offset;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            src.PlacedFootprint.Footprint.Width = static_cast<UINT>(region.width);
            src.PlacedFootprint.Footprint.Height = static_cast<UINT>(region.height);
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = placements[i].rowPitch;

            recording.List()->CopyTextureRegion(
                &dst,
                static_cast<UINT>(region.x), static_cast<UINT>(region.y), 0,
                &src, nullptr);
        }

        Barrier::Transition(recording.List(), atlas_, D3D12_RESOURCE_STATE_GENERIC_READ);

        // 中間バッファは GPU コピー完了まで生きていればよい（フェンス通過後に自動解放）
        recording.KeepAlive(std::move(staging));
    }

    MsdfGlyph MsdfFont::ResolveGlyph(char32_t codePoint) const
    {
        std::lock_guard lock(glyphMutex_);
        const auto it = glyphs_.find(codePoint);
        return (it != glyphs_.end()) ? it->second : notdefGlyph_;
    }

    void MsdfFont::RequestGlyphs(const std::vector<char32_t>& codePoints)
    {
        if (!dynamicGlyphsEnabled_ || atlasFull_.load(std::memory_order_relaxed)) {
            return;
        }

        std::lock_guard lock(glyphMutex_);

        for (char32_t codePoint : codePoints) {
            if (codePoint == U'\n' || codePoint == U'\r' || codePoint == U'\t') { continue; }
            // 登録済み・要求済みは弾く。requestedCodePoints_ には
            // 「焼いてみたが収録が無かった」文字も残るので、無駄な再要求も起きない
            if (requestedCodePoints_.insert(codePoint).second) {
                pendingQueue_.push_back(codePoint);
            }
        }

        KickBakeTaskLocked();
    }

    void MsdfFont::KickBakeTaskLocked()
    {
        if (pendingQueue_.empty() || !threadPool_) { return; }

        bool expected = false;
        if (!bakeRunning_.compare_exchange_strong(expected, true)) {
            return; // 既に走っている。走行中のワーカーがキューを拾う
        }

        bakeTask_ = threadPool_->Submit("MsdfBakeQueue", [this] { ProcessBakeQueue(); });
    }

    void MsdfFont::WriteMirrorLocked(const MsdfGlyphBitmap& baked)
    {
        if (!baked.glyph.hasBitmap) { return; }

        const int atlasWidth = static_cast<int>(atlasSize_.x);
        const int atlasHeight = static_cast<int>(atlasSize_.y);
        const size_t pageBytes = static_cast<size_t>(atlasWidth) * atlasHeight * 4;

        // 新しい枚に手を出したら控えを伸ばす（使う前から全枚ぶん抱えない）
        const int requiredPages = static_cast<int>(baked.glyph.page) + 1;
        if (requiredPages > mirrorPageCount_) {
            atlasMirror_.resize(pageBytes * requiredPages, 0);
            mirrorPageCount_ = requiredPages;
        }

        const size_t pageOffset = pageBytes * baked.glyph.page;
        for (int y = 0; y < baked.height; ++y) {
            const size_t dstOffset = pageOffset
                + (static_cast<size_t>(baked.atlasY + y) * atlasWidth + baked.atlasX) * 4;
            std::memcpy(atlasMirror_.data() + dstOffset,
                baked.pixels.data() + static_cast<size_t>(y) * baked.width * 4,
                static_cast<size_t>(baked.width) * 4);
        }
    }

    void MsdfFont::ProcessBakeQueue()
    {
        // アトラスの切り出しだけをロックで守る。
        // 距離場の計算はこの外で走るので、複数ワーカーで並列に焼ける
        const MsdfGlyphAllocateFn allocate =
            [this](int w, int h, int& page, int& x, int& y) {
            std::lock_guard lock(allocatorMutex_);
            const bool ok = allocator_.Allocate(w, h, page, x, y);
            if (ok) {
                atlasOccupancy_.store(allocator_.GetOccupancy(), std::memory_order_relaxed);
                usedPageCount_.store(allocator_.GetUsedPageCount(), std::memory_order_relaxed);
            }
            return ok;
            };

        for (;;) {
            std::vector<char32_t> batch;
            {
                std::lock_guard lock(glyphMutex_);
                if (pendingQueue_.empty()) {
                    break;
                }
                const size_t take = (std::min)(pendingQueue_.size(), kMaxGlyphsPerBakeBatch);
                batch.assign(pendingQueue_.begin(), pendingQueue_.begin() + take);
                pendingQueue_.erase(pendingQueue_.begin(), pendingQueue_.begin() + take);
            }

            // ── 並列に焼く ──────────────────────────────────────
            // ThreadPool::Wait はワーカーの中から呼んでも、待つ間に
            // キューの仕事を自分で引き受けるのでデッドロックしない
            std::vector<std::future<MsdfGlyphBitmap>> futures;
            futures.reserve(batch.size());
            for (char32_t codePoint : batch) {
                futures.push_back(threadPool_->Submit("MsdfGlyphBake",
                    [this, codePoint, &allocate] {
                        return MsdfFontBaker::BakeGlyph(
                            faceChain_, codePoint, bakeSettings_, allocate, &outlineMutex_);
                    }));
            }

            std::vector<MsdfGlyphBitmap> baked;
            std::vector<char32_t> bakedCodePoints;
            baked.reserve(batch.size());
            bakedCodePoints.reserve(batch.size());
            bool ranOutOfSpace = false;

            for (size_t i = 0; i < futures.size(); ++i) {
                threadPool_->Wait(futures[i]);
                MsdfGlyphBitmap result = futures[i].get();
                if (!result.valid) {
                    // 収録が無い / 棚が尽きた。glyphs_ へ入れないので .notdef が出る
                    if (atlasOccupancy_.load(std::memory_order_relaxed) >= 1.0f) {
                        ranOutOfSpace = true;
                    }
                    continue;
                }
                bakedCodePoints.push_back(batch[i]);
                baked.push_back(std::move(result));
            }

            // 絵を持つものだけ GPU へ送る（空白は advance だけなので転送不要）
            std::vector<AtlasUploadRegion> regions;
            regions.reserve(baked.size());
            for (const MsdfGlyphBitmap& item : baked) {
                if (item.glyph.hasBitmap) {
                    regions.push_back(AtlasUploadRegion{
                        item.glyph.page, item.atlasX, item.atlasY,
                        item.width, item.height, item.pixels.data() });
                }
            }
            UploadRegions(regions);

            {
                std::lock_guard lock(glyphMutex_);
                for (size_t i = 0; i < baked.size(); ++i) {
                    glyphs_[bakedCodePoints[i]] = baked[i].glyph;
                    WriteMirrorLocked(baked[i]);
                }
            }

            if (ranOutOfSpace && !atlasFull_.exchange(true)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                    "MsdfFont: アトラス {}x{} x{}枚 を使い切りました。"
                    "以降の新規文字は .notdef で描画されます。"
                    "MsdfBakeSettings::atlasPageCount を増やしてください",
                    static_cast<int>(atlasSize_.x), static_cast<int>(atlasSize_.y),
                    bakeSettings_.atlasPageCount);
            }

            if (!baked.empty()) {
                unsavedGlyphCount_.fetch_add(static_cast<uint32_t>(baked.size()),
                    std::memory_order_relaxed);
                // 表が変わったことを描画側へ知らせる（UIText が頂点を組み直す）
                glyphGeneration_.fetch_add(1, std::memory_order_release);
            }

            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                "MsdfFont: 実行時ベイク {} 件中 {} 件成功（{} 枚目 / 使用量 {:.1f}%）",
                batch.size(), baked.size(),
                usedPageCount_.load(std::memory_order_relaxed),
                atlasOccupancy_.load(std::memory_order_relaxed) * 100.0f);
        }

        // キューが空いた区切りで、溜まったぶんをキャッシュへ書き戻す。
        // 毎バッチ書くとアトラス全面の書き込みが頻発するので件数で間引く
        if (writeBackEnabled_
            && unsavedGlyphCount_.load(std::memory_order_relaxed) >= kCacheWriteBackThreshold) {
            SaveCacheSnapshot();
        }

        // ここが最後の this へのアクセスになるようにする
        // （デストラクタの wait と噛み合わせるため）
        bakeRunning_.store(false, std::memory_order_release);
    }

    bool MsdfFont::SaveCacheSnapshot()
    {
        if (cachePath_.empty()) { return false; }

        MsdfBakeResult snapshot{};
        {
            std::lock_guard lock(glyphMutex_);
            if (atlasMirror_.empty()) { return false; }

            snapshot.success = true;
            snapshot.atlasWidth = static_cast<int>(atlasSize_.x);
            snapshot.atlasHeight = static_cast<int>(atlasSize_.y);
            snapshot.pageCount = mirrorPageCount_;
            snapshot.pixels = atlasMirror_;
            snapshot.glyphs = glyphs_;
            snapshot.notdefGlyph = notdefGlyph_;
            snapshot.metrics = metrics_;
            snapshot.settings = bakeSettings_;
        }
        int usedPages = 1;
        {
            std::lock_guard lock(allocatorMutex_);
            snapshot.allocatorState = allocator_.GetState();
            usedPages = allocator_.GetUsedPageCount();
        }

        // まだ手を付けていない枚は保存しない（丸ごとゼロの数 MB を書くだけになる）
        usedPages = std::clamp(usedPages, 1, snapshot.pageCount);
        if (usedPages < snapshot.pageCount) {
            const size_t pageBytes =
                static_cast<size_t>(snapshot.atlasWidth) * snapshot.atlasHeight * 4;
            snapshot.pixels.resize(pageBytes * usedPages);
            snapshot.pageCount = usedPages;
        }

        const uint32_t saved = unsavedGlyphCount_.exchange(0, std::memory_order_relaxed);

        if (!MsdfFontCache::Save(cachePath_, snapshot)) {
            // 失敗したら次の機会に再挑戦できるよう件数を戻す
            unsavedGlyphCount_.fetch_add(saved, std::memory_order_relaxed);
            return false;
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "MSDF アトラスをキャッシュへ書き戻しました: {} ({} グリフ / {} 枚 / 実行時追加 {} 件)",
            Logger::GetInstance().PathToUtf8(cachePath_),
            snapshot.glyphs.size(), snapshot.pageCount, saved);
        return true;
    }

    size_t MsdfFont::GetGlyphCount() const
    {
        std::lock_guard lock(glyphMutex_);
        return glyphs_.size();
    }

    size_t MsdfFont::GetPendingGlyphCount() const
    {
        std::lock_guard lock(glyphMutex_);
        return pendingQueue_.size();
    }

    float MsdfFont::GetAtlasOccupancy() const
    {
        // ワーカーが更新する atomic を読むだけ。ここでロックを取ると
        // ImGui のデバッグ表示がベイク 1 件ぶん止まる
        return atlasOccupancy_.load(std::memory_order_relaxed);
    }

    bool MsdfFont::DumpAtlas(int page, const std::filesystem::path& outPath) const
    {
        std::vector<uint8_t> snapshot;
        int width = 0;
        int height = 0;
        {
            std::lock_guard lock(glyphMutex_);
            if (page < 0 || page >= mirrorPageCount_) { return false; }
            width = static_cast<int>(atlasSize_.x);
            height = static_cast<int>(atlasSize_.y);
            const size_t pageBytes = static_cast<size_t>(width) * height * 4;
            const uint8_t* begin = atlasMirror_.data() + pageBytes * page;
            snapshot.assign(begin, begin + pageBytes);
        }
        return MsdfFontBaker::SaveAtlasPng(snapshot.data(), width, height, outPath);
    }
}
