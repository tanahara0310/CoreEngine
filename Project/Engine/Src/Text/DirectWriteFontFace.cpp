#include "pch.h"
#include "Text/DirectWriteFontFace.h"

#include "Utility/Logger/Logger.h"

// ID2D1SimplifiedGeometrySink（= IDWriteGeometrySink）の実体定義に必要。
// D2D の API は一切呼ばないので d2d1.lib のリンクは不要。
#include <d2d1.h>
#include <dwrite_3.h>

#include <Externals/msdfgen/msdfgen.h>

#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace CoreEngine
{
    namespace
    {
        /// @brief DirectWrite のアウトラインを msdfgen::Shape へ組み立てるシンク
        /// @details
        ///  スタック上に置いて GetGlyphRunOutline() へ渡すだけなので、
        ///  IUnknown の参照カウントは実装しない（AddRef/Release は常に 1 を返す）。
        ///
        ///  座標変換について:
        ///   DirectWrite は D2D 準拠で **Y が下向き**（ベースライン原点、上方向が負）の
        ///   輪郭を返す。一方 msdfgen の既定は Y 上正なので、ここで Y を反転して
        ///   フォント設計空間へ戻す。反転すると輪郭の巻き方向も同時に元へ戻るため、
        ///   FreeType の FT_Outline_Decompose と同じ結果になる。
        class GlyphOutlineSink final : public IDWriteGeometrySink
        {
        public:
            explicit GlyphOutlineSink(msdfgen::Shape& shape) : shape_(shape) {}

            // ── ID2D1SimplifiedGeometrySink ────────────────────────────
            void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
            void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}

            void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override
            {
                contour_ = &shape_.addContour();
                start_ = current_ = ToPoint(start);
            }

            void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) override
            {
                if (!contour_ || !points) { return; }
                for (UINT32 i = 0; i < count; ++i) {
                    AddLineTo(ToPoint(points[i]));
                }
            }

            void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) override
            {
                if (!contour_ || !beziers) { return; }
                for (UINT32 i = 0; i < count; ++i) {
                    // TrueType の 2 次ベジェも DirectWrite が 3 次へ変換して渡してくる。
                    // msdfgen は 3 次をそのまま扱えるので変換不要。
                    const msdfgen::Point2 c1 = ToPoint(beziers[i].point1);
                    const msdfgen::Point2 c2 = ToPoint(beziers[i].point2);
                    const msdfgen::Point2 end = ToPoint(beziers[i].point3);

                    if (end == current_ && c1 == current_ && c2 == current_) {
                        continue; // 退化したセグメントは捨てる
                    }
                    contour_->addEdge(msdfgen::EdgeHolder(current_, c1, c2, end));
                    current_ = end;
                }
            }

            void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override
            {
                // DirectWrite は終点＝始点を明示しないことがあるので、開いていれば閉じる。
                // 開いたままの輪郭を msdfgen へ渡すと内外判定が壊れる。
                if (contour_) {
                    AddLineTo(start_);
                    if (contour_->edges.empty()) {
                        // 実体の無い輪郭は残さない（Shape::validate が落ちる）
                        shape_.contours.pop_back();
                    }
                }
                contour_ = nullptr;
            }

            HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

            // ── IUnknown（スタック上の一時オブジェクトなので実装しない）──
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
            ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
            ULONG STDMETHODCALLTYPE Release() override { return 1; }

        private:
            /// @brief D2D の Y 下正座標を msdfgen の Y 上正座標へ変換する
            static msdfgen::Point2 ToPoint(D2D1_POINT_2F p)
            {
                return msdfgen::Point2(static_cast<double>(p.x), -static_cast<double>(p.y));
            }

            void AddLineTo(const msdfgen::Point2& next)
            {
                if (next == current_) { return; } // 長さ 0 の辺は距離場を壊す
                contour_->addEdge(msdfgen::EdgeHolder(current_, next));
                current_ = next;
            }

            msdfgen::Shape& shape_;
            msdfgen::Contour* contour_ = nullptr;
            msdfgen::Point2 start_{};
            msdfgen::Point2 current_{};
        };
    } // namespace

    // ──────────────────────────────────────────────────────────────

    DirectWriteFontFace::DirectWriteFontFace() = default;
    DirectWriteFontFace::~DirectWriteFontFace() = default;

    IDWriteFactory* DirectWriteFontFace::AcquireFactory()
    {
        // プロセス内で 1 つあれば足りる。DWRITE_FACTORY_TYPE_SHARED は
        // 内部キャッシュ（フォントの解析結果）を再利用するので、
        // フォントを焼き直すたびに作り直すより速い。
        static ComPtr<IDWriteFactory> factory;
        if (!factory) {
            const HRESULT hr = DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
            if (FAILED(hr)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                    "DWriteCreateFactory に失敗しました (HRESULT: 0x{:08X})",
                    static_cast<unsigned int>(hr));
                return nullptr;
            }
        }
        return factory.Get();
    }

    bool DirectWriteFontFace::LoadFromFile(const std::wstring& filePath, uint32_t faceIndex)
    {
        IDWriteFactory* factory = AcquireFactory();
        if (!factory) { return false; }

        ComPtr<IDWriteFontFile> fontFile;
        HRESULT hr = factory->CreateFontFileReference(filePath.c_str(), nullptr, &fontFile);
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "フォントファイルを開けませんでした: {} (HRESULT: 0x{:08X})",
                Logger::GetInstance().PathToUtf8(filePath),
                static_cast<unsigned int>(hr));
            return false;
        }

        // .ttc も .otf も同じ入口。フェイス種別は DirectWrite が判別する
        BOOL isSupported = FALSE;
        DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
        DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
        UINT32 faceCount = 0;
        hr = fontFile->Analyze(&isSupported, &fileType, &faceType, &faceCount);
        if (FAILED(hr) || !isSupported) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "対応していないフォント形式です: {}",
                Logger::GetInstance().PathToUtf8(filePath));
            return false;
        }
        if (faceIndex >= faceCount) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "フェイス番号が範囲外です: {} (収録数 {})", faceIndex, faceCount);
            return false;
        }

        ComPtr<IDWriteFontFace> face;
        IDWriteFontFile* files[] = { fontFile.Get() };
        hr = factory->CreateFontFace(faceType, 1, files, faceIndex,
            DWRITE_FONT_SIMULATIONS_NONE, &face);
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "CreateFontFace に失敗しました (HRESULT: 0x{:08X})",
                static_cast<unsigned int>(hr));
            return false;
        }

        fontFace_ = face;
        displayName_ = filePath;
        CacheMetrics();
        return true;
    }

    bool DirectWriteFontFace::LoadFromSystem(const std::wstring& familyName)
    {
        IDWriteFactory* factory = AcquireFactory();
        if (!factory) { return false; }

        ComPtr<IDWriteFontCollection> collection;
        if (FAILED(factory->GetSystemFontCollection(&collection, FALSE))) {
            return false;
        }

        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        if (FAILED(collection->FindFamilyName(familyName.c_str(), &familyIndex, &exists)) || !exists) {
            return false;
        }

        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(familyIndex, &family))) {
            return false;
        }

        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            &font))) {
            return false;
        }

        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) {
            return false;
        }

        fontFace_ = face;
        displayName_ = familyName;
        CacheMetrics();
        return true;
    }

    std::wstring DirectWriteFontFace::LoadFromSystemPreferred(const std::vector<std::wstring>& familyNames)
    {
        for (const auto& name : familyNames) {
            if (LoadFromSystem(name)) {
                return name;
            }
        }
        return {};
    }

    void DirectWriteFontFace::CacheMetrics()
    {
        DWRITE_FONT_METRICS fm{};
        fontFace_->GetMetrics(&fm);

        designUnitsPerEm_ = static_cast<float>(fm.designUnitsPerEm);
        if (designUnitsPerEm_ <= 0.0f) {
            designUnitsPerEm_ = 1.0f;
        }

        // 全て em 単位へ正規化して持つ（表示サイズを掛けるだけで px になる）
        metrics_.ascender = static_cast<float>(fm.ascent) / designUnitsPerEm_;
        metrics_.descender = -static_cast<float>(fm.descent) / designUnitsPerEm_;
        metrics_.lineHeight =
            static_cast<float>(fm.ascent + fm.descent + fm.lineGap) / designUnitsPerEm_;
    }

    uint16_t DirectWriteFontFace::GetGlyphIndex(char32_t codePoint) const
    {
        if (!fontFace_) { return 0; }

        const UINT32 cp = static_cast<UINT32>(codePoint);
        UINT16 glyphIndex = 0;
        if (FAILED(fontFace_->GetGlyphIndices(&cp, 1, &glyphIndex))) {
            return 0;
        }
        return glyphIndex;
    }

    float DirectWriteFontFace::GetAdvance(uint16_t glyphIndex) const
    {
        if (!fontFace_) { return 0.0f; }

        DWRITE_GLYPH_METRICS gm{};
        if (FAILED(fontFace_->GetDesignGlyphMetrics(&glyphIndex, 1, &gm, FALSE))) {
            return 0.0f;
        }
        return static_cast<float>(gm.advanceWidth) / designUnitsPerEm_;
    }

    bool DirectWriteFontFace::BuildShape(uint16_t glyphIndex, msdfgen::Shape& outShape) const
    {
        if (!fontFace_) { return false; }

        GlyphOutlineSink sink(outShape);

        // emSize = 1.0 を渡すと、輪郭が em 単位（0..1 前後）で出てくる。
        // 以降のスケーリングは msdfgen の Projection 側で一括して行うので、
        // ここでピクセルサイズを混ぜないほうが座標系が 1 本で済む。
        const HRESULT hr = fontFace_->GetGlyphRunOutline(
            1.0f,
            &glyphIndex,
            /*glyphAdvances*/ nullptr,
            /*glyphOffsets*/ nullptr,
            /*glyphCount*/ 1,
            /*isSideways*/ FALSE,
            /*isRightToLeft*/ FALSE,
            &sink);

        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "GetGlyphRunOutline に失敗しました (glyph: {}, HRESULT: 0x{:08X})",
                glyphIndex, static_cast<unsigned int>(hr));
            return false;
        }

        return true;
    }
}
