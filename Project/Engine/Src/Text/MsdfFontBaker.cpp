#include "pch.h"
#include "Text/MsdfFontBaker.h"

#include "Text/DirectWriteFontFace.h"
#include "Utility/Logger/Logger.h"

#include <Externals/msdfgen/msdfgen.h>
#include <externals/DirectXTex/DirectXTex.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace CoreEngine
{
    namespace
    {
        /// msdfgen 標準のコーナー判定角（ラジアン）。
        /// これより鋭い折れをコーナーとみなしてチャンネルを切り替える
        constexpr double kEdgeColoringAngleThreshold = 3.0;

        /// フォント側に .notdef の絵が無かったときに自前で描く豆腐の寸法（em 単位）
        constexpr double kFallbackBoxAdvance = 0.5;
        constexpr double kFallbackBoxMargin = 0.06;
        constexpr double kFallbackBoxTop = 0.68;
        constexpr double kFallbackBoxStroke = 0.05;

        /// @brief 中抜きの矩形（豆腐）を組み立てる
        void BuildFallbackBoxShape(msdfgen::Shape& shape)
        {
            const double l = kFallbackBoxMargin;
            const double r = kFallbackBoxAdvance - kFallbackBoxMargin;
            const double b = 0.0;
            const double t = kFallbackBoxTop;
            const double s = kFallbackBoxStroke;

            // 外周（反時計回り）
            msdfgen::Contour& outer = shape.addContour();
            outer.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(l, b), msdfgen::Point2(r, b)));
            outer.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(r, b), msdfgen::Point2(r, t)));
            outer.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(r, t), msdfgen::Point2(l, t)));
            outer.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(l, t), msdfgen::Point2(l, b)));

            // 内周（時計回り＝逆巻き。非ゼロワインディングで中が抜ける）
            msdfgen::Contour& inner = shape.addContour();
            inner.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(l + s, b + s), msdfgen::Point2(l + s, t - s)));
            inner.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(l + s, t - s), msdfgen::Point2(r - s, t - s)));
            inner.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(r - s, t - s), msdfgen::Point2(r - s, b + s)));
            inner.addEdge(msdfgen::EdgeHolder(msdfgen::Point2(r - s, b + s), msdfgen::Point2(l + s, b + s)));
        }

        /// @brief 輪郭から距離場を焼き、アトラス上の場所まで確保する共通処理
        /// @details
        ///  「測る → 確保 → 焼く」の 3 段に分かれている。
        ///  確保だけが共有状態（アロケータ）に触るので、そこだけを
        ///  呼び出し側のコールバックへ委ね、前後はロック無しで走らせる。
        MsdfGlyphBitmap GenerateFromShape(
            msdfgen::Shape& shape,
            float advance,
            const MsdfBakeSettings& settings,
            const MsdfGlyphAllocateFn& allocate)
        {
            MsdfGlyphBitmap out{};
            out.glyph.advance = advance;

            if (shape.contours.empty()) {
                // 空白文字。絵は無いが advance は要る
                out.valid = true;
                out.glyph.hasBitmap = false;
                return out;
            }
            if (!shape.validate()) {
                return out; // valid = false
            }
            shape.normalize();

            const double pixelsPerEm = static_cast<double>(settings.glyphPixelSize);
            // 距離場のマージン。輪郭の外側 pxRange/2 px まで距離を持たせる
            const double halfRangeEm = 0.5 * settings.pxRange / pixelsPerEm;

            // ── ①測る ────────────────────────────────────────────
            const msdfgen::Shape::Bounds bounds = shape.getBounds();

            const double left = bounds.l - halfRangeEm;
            const double bottom = bounds.b - halfRangeEm;

            int width = static_cast<int>(std::ceil((bounds.r + halfRangeEm - left) * pixelsPerEm));
            int height = static_cast<int>(std::ceil((bounds.t + halfRangeEm - bottom) * pixelsPerEm));
            width = (std::max)(width, 1);
            height = (std::max)(height, 1);

            // ── ②確保（ここだけ共有状態）──────────────────────────
            int atlasPage = 0;
            int atlasX = 0;
            int atlasY = 0;
            if (!allocate || !allocate(width, height, atlasPage, atlasX, atlasY)) {
                return out; // アトラスを使い切った
            }

            // 整数化した幅・高さに合わせて右上端を取り直す。
            // こうしておくと「plane 境界 ↔ UV 矩形」が誤差なく 1 対 1 で対応する
            const double right = left + width / pixelsPerEm;
            const double top = bottom + height / pixelsPerEm;

            // ── ③焼く（重い。ロックの外で走らせたい部分）──────────
            // コーナーで輪郭を切り、隣り合う辺が 1 チャンネルだけ共有するように
            // RGB を割り当てる。MSDF が角を保てるのはこの彩色があるからで、
            // generateMTSDF より先に必ず通す必要がある
            msdfgen::edgeColoringSimple(shape, kEdgeColoringAngleThreshold);

            const msdfgen::Projection projection(
                msdfgen::Vector2(pixelsPerEm, pixelsPerEm),
                msdfgen::Vector2(-left, -bottom));
            const msdfgen::Range distanceRange(settings.pxRange / pixelsPerEm);
            const msdfgen::MSDFGeneratorConfig generatorConfig; // 既定でエラー訂正が有効

            std::vector<float> scratch(static_cast<size_t>(width) * height * 4, 0.0f);

            // 既定の Y 上正で焼く（メモリ上の行 0 ＝ グリフの下端）
            msdfgen::BitmapRef<float, 4> bitmap(scratch.data(), width, height);
            msdfgen::generateMTSDF(bitmap, shape, projection, distanceRange, generatorConfig);

            // 出力は top-down にそろえるので、行を反転しながら詰める
            out.pixels.resize(static_cast<size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y) {
                const int srcRow = height - 1 - y;
                uint8_t* dstRow = out.pixels.data() + static_cast<size_t>(y) * width * 4;
                for (int x = 0; x < width; ++x) {
                    const float* src = bitmap(x, srcRow);
                    uint8_t* dst = dstRow + static_cast<size_t>(x) * 4;
                    dst[0] = msdfgen::pixelFloatToByte(src[0]);
                    dst[1] = msdfgen::pixelFloatToByte(src[1]);
                    dst[2] = msdfgen::pixelFloatToByte(src[2]);
                    dst[3] = msdfgen::pixelFloatToByte(src[3]);
                }
            }

            out.valid = true;
            out.atlasX = atlasX;
            out.atlasY = atlasY;
            out.width = width;
            out.height = height;

            out.glyph.hasBitmap = true;
            out.glyph.page = static_cast<uint32_t>(atlasPage);
            out.glyph.planeLeft = static_cast<float>(left);
            out.glyph.planeBottom = static_cast<float>(bottom);
            out.glyph.planeRight = static_cast<float>(right);
            out.glyph.planeTop = static_cast<float>(top);
            out.glyph.uvLeft = static_cast<float>(atlasX) / settings.atlasWidth;
            out.glyph.uvTop = static_cast<float>(atlasY) / settings.atlasHeight;
            out.glyph.uvRight = static_cast<float>(atlasX + width) / settings.atlasWidth;
            out.glyph.uvBottom = static_cast<float>(atlasY + height) / settings.atlasHeight;

            return out;
        }
    } // namespace

    // ──────────────────────────────────────────────────────────────

    MsdfGlyphBitmap MsdfFontBaker::BakeGlyph(
        const std::vector<const DirectWriteFontFace*>& faceChain,
        char32_t codePoint,
        const MsdfBakeSettings& settings,
        const MsdfGlyphAllocateFn& allocate,
        std::mutex* outlineMutex,
        bool* outUsedFallback)
    {
        if (outUsedFallback) { *outUsedFallback = false; }

        msdfgen::Shape shape;
        float advance = 0.0f;

        {
            // DirectWrite への問い合わせは、必要なら直列化する。
            // ここは軽い工程なので、まとめてロックしても並列度はほぼ落ちない
            std::unique_lock<std::mutex> lock;
            if (outlineMutex) { lock = std::unique_lock<std::mutex>(*outlineMutex); }

            // フォントフォールバック: 先頭から順に、その文字を収録しているフォントを探す
            const DirectWriteFontFace* sourceFace = nullptr;
            uint16_t glyphIndex = 0;
            for (size_t i = 0; i < faceChain.size(); ++i) {
                const DirectWriteFontFace* face = faceChain[i];
                if (!face || !face->IsValid()) { continue; }
                const uint16_t index = face->GetGlyphIndex(codePoint);
                if (index != 0) {
                    sourceFace = face;
                    glyphIndex = index;
                    if (i > 0 && outUsedFallback) { *outUsedFallback = true; }
                    break;
                }
            }

            if (!sourceFace) {
                return {}; // どのフォントにも無い
            }
            if (!sourceFace->BuildShape(glyphIndex, shape)) {
                return {};
            }
            advance = sourceFace->GetAdvance(glyphIndex);
        }

        return GenerateFromShape(shape, advance, settings, allocate);
    }

    MsdfGlyphBitmap MsdfFontBaker::BakeNotdef(
        const DirectWriteFontFace& primaryFace,
        const MsdfBakeSettings& settings,
        const MsdfGlyphAllocateFn& allocate)
    {
        msdfgen::Shape shape;
        primaryFace.BuildShape(0, shape);

        float advance = primaryFace.GetAdvance(0);

        if (shape.contours.empty()) {
            // フォントの .notdef が空だった。自前の豆腐で代用する
            shape = msdfgen::Shape();
            BuildFallbackBoxShape(shape);
            advance = static_cast<float>(kFallbackBoxAdvance);
        }
        if (advance <= 0.0f) {
            advance = static_cast<float>(kFallbackBoxAdvance);
        }

        MsdfGlyphBitmap result = GenerateFromShape(shape, advance, settings, allocate);
        if (!result.valid) {
            // フォント由来の輪郭が不正だった場合の最後の砦
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "MsdfFontBaker: .notdef を焼けませんでした。豆腐で代用します");
            msdfgen::Shape box;
            BuildFallbackBoxShape(box);
            result = GenerateFromShape(box, static_cast<float>(kFallbackBoxAdvance),
                settings, allocate);
        }
        return result;
    }

    MsdfBakeResult MsdfFontBaker::Bake(
        const std::vector<const DirectWriteFontFace*>& faceChain,
        const std::vector<char32_t>& codePoints,
        const MsdfBakeSettings& settings)
    {
        MsdfBakeResult result{};
        result.settings = settings;

        if (faceChain.empty() || !faceChain.front() || !faceChain.front()->IsValid()) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfFontBaker: フォントが読み込まれていません");
            return result;
        }
        if (settings.glyphPixelSize <= 0 || settings.atlasWidth <= 0
            || settings.atlasHeight <= 0 || settings.atlasPageCount <= 0) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfFontBaker: アトラス設定が不正です");
            return result;
        }

        const auto startTime = std::chrono::steady_clock::now();

        result.atlasWidth = settings.atlasWidth;
        result.atlasHeight = settings.atlasHeight;
        result.pageCount = settings.atlasPageCount;

        const size_t pageBytes =
            static_cast<size_t>(settings.atlasWidth) * settings.atlasHeight * 4;
        result.pixels.assign(pageBytes * settings.atlasPageCount, 0);

        MsdfAtlasAllocator allocator;
        allocator.Initialize(settings.atlasWidth, settings.atlasHeight,
            settings.atlasPageCount, settings.padding);

        // ここは単一スレッドなのでロック不要
        const MsdfGlyphAllocateFn allocate =
            [&allocator](int w, int h, int& page, int& x, int& y) {
            return allocator.Allocate(w, h, page, x, y);
            };

        // 焼けた画素をアトラスへ書き写す
        const auto blit = [&result, &settings, pageBytes](const MsdfGlyphBitmap& baked) {
            const size_t pageOffset = pageBytes * baked.glyph.page;
            for (int y = 0; y < baked.height; ++y) {
                const size_t dstOffset = pageOffset
                    + (static_cast<size_t>(baked.atlasY + y) * settings.atlasWidth + baked.atlasX) * 4;
                std::memcpy(result.pixels.data() + dstOffset,
                    baked.pixels.data() + static_cast<size_t>(y) * baked.width * 4,
                    static_cast<size_t>(baked.width) * 4);
            }
            };

        // ── ①.notdef（豆腐）を必ず 1 つ用意する ──────────────────
        {
            const MsdfGlyphBitmap notdef = BakeNotdef(*faceChain.front(), settings, allocate);
            if (notdef.valid) {
                result.notdefGlyph = notdef.glyph;
                if (notdef.glyph.hasBitmap) {
                    blit(notdef);
                    ++result.bakedGlyphCount;
                }
            }
        }

        // ── ②各文字を焼く ────────────────────────────────────────
        std::vector<char32_t> uniqueCodePoints = codePoints;
        std::sort(uniqueCodePoints.begin(), uniqueCodePoints.end());
        uniqueCodePoints.erase(
            std::unique(uniqueCodePoints.begin(), uniqueCodePoints.end()),
            uniqueCodePoints.end());

        for (char32_t codePoint : uniqueCodePoints) {
            bool usedFallback = false;
            const MsdfGlyphBitmap baked =
                BakeGlyph(faceChain, codePoint, settings, allocate, nullptr, &usedFallback);

            if (!baked.valid) {
                // 収録が無い / アトラスが尽きた。glyphs へ入れないので、
                // 描画時に MsdfFont::ResolveGlyph が .notdef へ倒す
                result.missingCodePoints.push_back(codePoint);
                continue;
            }
            if (usedFallback) { ++result.fallbackGlyphCount; }

            if (baked.glyph.hasBitmap) {
                blit(baked);
                ++result.bakedGlyphCount;
            } else {
                ++result.blankGlyphCount;
            }

            result.glyphs[codePoint] = baked.glyph;
        }

        result.allocatorState = allocator.GetState();
        result.metrics = faceChain.front()->GetMetrics();
        result.success = true;
        result.bakeSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startTime).count();

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "MSDF アトラスを生成しました: {}x{} x{}枚 / 描画グリフ {} 件・空白 {} 件・代替フォント {} 件 / "
            "{}px 焼き・pxRange {} / 使用量 {:.1f}% / {:.2f} 秒",
            result.atlasWidth, result.atlasHeight, result.pageCount,
            result.bakedGlyphCount, result.blankGlyphCount, result.fallbackGlyphCount,
            settings.glyphPixelSize, settings.pxRange,
            allocator.GetOccupancy() * 100.0f, result.bakeSeconds);

        if (!result.missingCodePoints.empty()) {
            // 消えた文字を黙って見逃さないよう、どの文字が無かったかを必ず残す
            std::string list;
            for (size_t i = 0; i < result.missingCodePoints.size() && i < 32; ++i) {
                list += std::format("U+{:04X} ",
                    static_cast<uint32_t>(result.missingCodePoints[i]));
            }
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "MsdfFontBaker: 焼けなかった文字が {} 件あります（.notdef で描画）: {}",
                result.missingCodePoints.size(), list);
        }

        return result;
    }

    bool MsdfFontBaker::SaveAtlasPng(const MsdfBakeResult& result, int page,
        const std::filesystem::path& outPath)
    {
        if (!result.success || page < 0 || page >= result.pageCount) { return false; }
        const size_t pageBytes =
            static_cast<size_t>(result.atlasWidth) * result.atlasHeight * 4;
        return SaveAtlasPng(result.pixels.data() + pageBytes * page,
            result.atlasWidth, result.atlasHeight, outPath);
    }

    bool MsdfFontBaker::SaveAtlasPng(const uint8_t* pixels, int width, int height,
        const std::filesystem::path& outPath)
    {
        if (!pixels || width <= 0 || height <= 0) {
            return false;
        }

        // アルファには真の SDF が入っているため、そのまま書くと
        // ビューアで半透明になって MSDF の色が読めない。
        // 目視確認が目的なので不透明に潰した複製を書き出す。
        const size_t byteCount = static_cast<size_t>(width) * height * 4;
        std::vector<uint8_t> opaque(pixels, pixels + byteCount);
        for (size_t i = 3; i < opaque.size(); i += 4) {
            opaque[i] = 0xFF;
        }

        DirectX::Image image{};
        image.width = static_cast<size_t>(width);
        image.height = static_cast<size_t>(height);
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        image.rowPitch = static_cast<size_t>(width) * 4;
        image.slicePitch = opaque.size();
        image.pixels = opaque.data();

        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);

        const HRESULT hr = DirectX::SaveToWICFile(
            image,
            DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
            outPath.c_str());

        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "MSDF アトラスの PNG 出力に失敗しました: {} (HRESULT: 0x{:08X})",
                Logger::GetInstance().PathToUtf8(outPath),
                static_cast<unsigned int>(hr));
            return false;
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "MSDF アトラスを書き出しました（目視確認用）: {}",
            Logger::GetInstance().PathToUtf8(outPath));
        return true;
    }
}
