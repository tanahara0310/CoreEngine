#pragma once

#include "Text/DirectWriteFontFace.h"
#include "Text/MsdfAtlasAllocator.h"
#include "Text/MsdfFontBaker.h"
#include "Text/MsdfFontTypes.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Math/Vector/Vector2.h"

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CoreEngine
{
    class GraphicsCore;
    class ThreadPool;

    /// @brief MSDF フォントの生成指定
    struct MsdfFontDesc
    {
        /// @brief フォントファイルのパス（指定するとフォールバック列の先頭になる）
        std::wstring filePath;
        uint32_t faceIndex = 0; ///< .ttc 内のフェイス番号

        /// @brief システムフォントのフォールバック列（先頭が優先）
        /// @details
        ///  「最初に見つかった 1 つを使う」ではなく **文字ごとに先頭から探す**。
        ///  先頭のフォントに無い文字は次のフォントから拾うので、
        ///  和文フォント → 欧文フォント → 記号フォント のように並べる。
        ///  行送り等のメトリクスは実際に採用できた先頭のフォントのものを使う。
        std::vector<std::wstring> systemFamilyNames;

        /// @brief 起動時に焼いておく文字（UTF-8）
        /// @note 動的グリフが有効なら、ここに無い文字も実行時に焼かれる。
        ///       最初のフレームから確実に出したい文字（HUD の固定ラベル等）を入れる。
        std::string charsetUtf8;

        /// @brief ASCII 可視文字（U+0020..U+007E）を charsetUtf8 に加えて焼く
        bool includeAscii = true;

        /// @brief 実行時に出てきた未登録文字を裏で焼いてアトラスへ追加する
        /// @details 和文はコードポイントを事前に列挙できない（シナリオ・アイテム名など）ため、
        ///          実運用ではこちらが本線になる。焼き上がるまでの数フレームは
        ///          .notdef（□）が出て、完了後に自動で差し替わる。
        bool enableDynamicGlyphs = true;

        MsdfBakeSettings bake{};

        /// @brief 焼き上がったアトラスをディスクへ残して次回以降再利用する
        /// @note 指定・フォント・文字集合のいずれかが変われば別キーになるので、
        ///       手で消す必要は無い
        bool useDiskCache = true;

        /// @brief 実行時に足したグリフもキャッシュへ書き戻す
        /// @details 有効にすると、2 回目以降の起動では前回出た文字が
        ///          最初から焼かれた状態で立ち上がる（□ が出る時間が消える）。
        bool writeBackRuntimeGlyphs = true;

        /// @brief キャッシュの置き場所（作業ディレクトリからの相対でよい）
        std::filesystem::path cacheDirectory = "Cache/FontCache";

        /// @brief 空でなければアトラス 0 枚目を PNG に書き出す（目視確認用）
        std::filesystem::path debugAtlasDumpPath;
    };

    /// @brief ランタイムで使う MSDF フォントアセット
    /// @details
    ///  アトラステクスチャ（GPU）とグリフメトリクス（CPU）の対を保持する。
    ///
    ///  アトラスは **Texture2DArray**。1 枚が埋まったら次の枚へ送るので、
    ///  和文を数千字扱っても破綻しない。枚数を頂点の texcoord.z に載せているため、
    ///  複数枚にまたがる文字列でもドローコールは 1 回のまま。
    ///
    ///  実行時に未登録の文字が要求されたら、ワーカースレッドで距離場を焼き、
    ///  アトラスの空き棚へ部分アップロードして差し込む。
    ///  アップロードは `UploadContext`（描画と同じキュー）へ積むので、
    ///  描画中のアトラス参照と衝突しない。
    ///
    /// @note アトラスは TextureManager を通さず自前で GPU へ上げている。
    ///       TextureManager の経路はミップ生成と BC3 圧縮を行うが、
    ///       どちらも距離場を破壊する（ミップ平均はコーナーの median を壊し、
    ///       ブロック圧縮は輪郭にノイズを載せる）ため通せない。
    class MsdfFont
    {
    public:
        MsdfFont() = default;
        ~MsdfFont();

        MsdfFont(const MsdfFont&) = delete;
        MsdfFont& operator=(const MsdfFont&) = delete;

        /// @brief フォントを読み込んでアトラスを生成し、GPU へ転送する
        /// @param graphicsCore デバイス・ディスクリプタ・アップロード経路の供給元
        /// @param threadPool 実行時ベイクを回すワーカー（nullptr なら動的グリフ無効）
        /// @param desc 生成指定
        bool Build(GraphicsCore* graphicsCore, ThreadPool* threadPool, const MsdfFontDesc& desc);

        /// @brief 使用可能か
        bool IsValid() const { return atlasHandle_.gpuHandle.ptr != 0; }

        /// @brief 未登録の文字を焼くよう要求する
        /// @details 既に登録済み・要求済みの文字は無視する。
        ///          焼き上がると GetGlyphGeneration() が進むので、
        ///          呼び出し側はそれを見てレイアウトを組み直す。
        void RequestGlyphs(const std::vector<char32_t>& codePoints);

        /// @brief グリフ表が更新されるたびに進む番号
        /// @details `UIText` はこれを毎フレーム見て、変化していたら頂点を組み直す。
        uint32_t GetGlyphGeneration() const { return glyphGeneration_.load(std::memory_order_acquire); }

        /// @brief 描画に使うグリフを解決する（未登録なら .notdef を返す）
        /// @details
        ///  未登録の文字を「描かない」で済ませると文字列が部分的に消え、
        ///  不具合として見えなくなる。必ず豆腐（□）を出して気付けるようにする。
        /// @note ワーカーがグリフ表を書き換えるので、参照ではなく値を返す
        ///       （unordered_map の再ハッシュで参照が無効化されるため）。
        MsdfGlyph ResolveGlyph(char32_t codePoint) const;

        /// @brief 未収録文字の代わりに描くグリフ（□）
        MsdfGlyph GetNotdefGlyph() const { return notdefGlyph_; }

        const MsdfFontMetrics& GetMetrics() const { return metrics_; }

        /// @brief アトラス SRV の GPU ハンドル（Texture2DArray）
        D3D12_GPU_DESCRIPTOR_HANDLE GetAtlasGpuHandle() const { return atlasHandle_.gpuHandle; }

        /// @brief アトラス 1 枚あたりの画素サイズ（シェーダーの screenPxRange 計算に要る）
        Vector2 GetAtlasSize() const { return atlasSize_; }

        /// @brief 距離場の有効範囲（px。シェーダーへそのまま渡す）
        float GetPxRange() const { return pxRange_; }

        /// @brief 焼いた解像度（1em あたりのピクセル数）
        /// @note 縁取り幅を em 単位で指定するための換算に要る
        int GetGlyphPixelSize() const { return bakeSettings_.glyphPixelSize; }

        /// @brief メトリクスの供給元になったフォント名（ログ・デバッグ表示用）
        const std::wstring& GetResolvedFontName() const { return resolvedFontName_; }

        /// @brief 実際に開けたフォールバック列（先頭が主フォント）
        const std::vector<std::wstring>& GetFontChainNames() const { return fontChainNames_; }

        /// @brief 実行時に足したグリフを含めてディスクキャッシュへ書き戻す
        /// @details 次回起動時に「前回出た文字」が最初から焼かれた状態になる。
        /// @note ベイクワーカーが走っている間は呼ばないこと（内部で整合を取る）
        bool SaveCacheSnapshot();

        // ===== デバッグ表示用 =====
        size_t GetGlyphCount() const;
        size_t GetPendingGlyphCount() const;
        float  GetAtlasOccupancy() const;
        int    GetUsedPageCount() const { return usedPageCount_.load(std::memory_order_relaxed); }
        int    GetPageCount() const { return bakeSettings_.atlasPageCount; }
        bool   IsAtlasFull() const { return atlasFull_.load(std::memory_order_relaxed); }

        /// @brief 現在のアトラス 1 枚（実行時に追加した分を含む）を PNG へ書き出す
        bool DumpAtlas(int page, const std::filesystem::path& outPath) const;

    private:
        /// @brief アトラスへ書き込む 1 領域
        struct AtlasUploadRegion
        {
            uint32_t page = 0;
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            const uint8_t* pixels = nullptr; ///< width*height*4・top-down・詰め済み
        };

        /// @brief Texture2DArray のアトラスと SRV を作る
        bool CreateAtlasTexture(int width, int height, int pageCount);

        /// @brief 指定領域をアトラスへ転送する（初回の全面転送と実行時の追加で共用）
        void UploadRegions(const std::vector<AtlasUploadRegion>& regions);

        /// @brief キューに溜まった文字を焼いてアトラスへ差し込む（ワーカー側の本体）
        void ProcessBakeQueue();

        /// @brief ベイク要求が残っていればワーカーを起こす（呼び出し前にロック済みであること）
        void KickBakeTaskLocked();

        /// @brief CPU 側の控えへ書き写す（必要なら枚を足す）。glyphMutex_ 取得済みで呼ぶこと
        void WriteMirrorLocked(const MsdfGlyphBitmap& baked);

        // ── 不変（Build 完了後は書き換えない）────────────────────
        GraphicsCore* graphicsCore_ = nullptr;
        ThreadPool* threadPool_ = nullptr;
        std::vector<std::unique_ptr<DirectWriteFontFace>> faces_;
        std::vector<const DirectWriteFontFace*> faceChain_;
        MsdfBakeSettings bakeSettings_{};
        MsdfFontMetrics metrics_{};
        MsdfGlyph notdefGlyph_{};
        Vector2 atlasSize_ = { 1.0f, 1.0f };
        float pxRange_ = 4.0f;
        bool dynamicGlyphsEnabled_ = false;
        bool writeBackEnabled_ = false;
        std::filesystem::path cachePath_;
        std::wstring resolvedFontName_;
        std::vector<std::wstring> fontChainNames_;

        // ── GPU 資源 ──────────────────────────────────────────────
        // ステートは GENERIC_READ が既定で、部分アップロードのときだけ
        // COPY_DEST へ落として戻す。触るのはベイクワーカーのみ
        GpuResource atlas_;
        DescriptorHandle atlasHandle_{};

        // ── ワーカーと共有する状態（glyphMutex_ が守る）──────────
        // @warning このロックは描画スレッドの ResolveGlyph も取る。
        //          距離場の計算のような重い処理をこの中で行ってはいけない
        //          （1 グリフ数 ms、Debug なら 100ms 単位で描画が止まる）
        mutable std::mutex glyphMutex_;
        std::unordered_map<char32_t, MsdfGlyph> glyphs_;
        std::unordered_set<char32_t> requestedCodePoints_; ///< 二重要求の抑止
        std::vector<char32_t> pendingQueue_;
        /// アトラスの CPU 側控え（使っている枚数ぶんだけ確保）。
        /// デバッグ出力とキャッシュ書き戻しに使う
        std::vector<uint8_t> atlasMirror_;
        int mirrorPageCount_ = 0;

        /// @brief 棚の切り出し状態
        /// @note ベイクを並列化したので、**確保だけ** をこのロックで守る。
        ///       距離場の計算はロックの外（MsdfFontBaker が「測る → 確保 → 焼く」に
        ///       分かれているのはこのため）
        mutable std::mutex allocatorMutex_;
        MsdfAtlasAllocator allocator_;

        /// @brief DirectWrite への問い合わせを直列化する
        /// @note DirectWrite 自体はスレッドセーフとされるが、
        ///       アウトライン取得は軽い工程なので念のため直列化しておく。
        ///       重い距離場の計算は並列のままなので実質のコストは無い
        mutable std::mutex outlineMutex_;

        std::atomic<uint32_t> glyphGeneration_{ 0 };
        std::atomic<bool> bakeRunning_{ false };
        std::atomic<bool> atlasFull_{ false };
        std::atomic<float> atlasOccupancy_{ 0.0f }; ///< デバッグ表示用（ロック無しで読む）
        std::atomic<int> usedPageCount_{ 1 };
        std::atomic<uint32_t> unsavedGlyphCount_{ 0 }; ///< キャッシュへ未反映のグリフ数
        std::future<void> bakeTask_;
    };
}
