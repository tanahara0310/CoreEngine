#include "pch.h"
#include "Text/FontManager.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Text/DirectWriteFontFace.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace CoreEngine
{
    namespace
    {
        // ── FNV-1a 64bit ────────────────────────────────────────
        constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
        constexpr uint64_t kFnvPrime = 1099511628211ULL;

        /// @brief DirectWrite が開けるフォントファイルの拡張子か
        /// @note エクスプローラから入れたファイルは ".TTF" のこともあるので小文字化して比べる
        bool IsFontFileExtension(const std::filesystem::path& extension)
        {
            std::wstring lowered = extension.wstring();
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

            return lowered == L".ttf" || lowered == L".otf"
                || lowered == L".ttc" || lowered == L".otc";
        }

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

    void FontManager::RegisterNamedFont(const std::string& name, const MsdfFontDesc& desc)
    {
        std::lock_guard lock(mutex_);
        namedFonts_[name] = desc;
    }

    MsdfFontDesc FontManager::MakeDefaultDesc()
    {
        MsdfFontDesc desc{};
        desc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"MS Gothic", L"Segoe UI" };
        desc.includeAscii = true;
        desc.charsetUtf8 =
            "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
            "アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン"
            "、。・ー「」（）";
        desc.enableDynamicGlyphs = true; // 足りない字は実行時に焼く
        return desc;
    }

    void FontManager::EnsureDefaultFontRegistered()
    {
        if (namedFonts_.contains(kDefaultFontName)) { return; }

        // エディタ上で作ったテキストが「フォント未設定で出ない」となるのを避けるため、
        // アプリが何も登録していなくても和文が出る指定を用意しておく。
        // アプリ側で RegisterNamedFont("Default", ...) すれば上書きできる
        namedFonts_[kDefaultFontName] = MakeDefaultDesc();

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "FontManager: 既定フォントを自動登録しました");
    }

    MsdfFont* FontManager::AcquireNamed(const std::string& name)
    {
        MsdfFontDesc desc{};
        {
            std::lock_guard lock(mutex_);
            EnsureDefaultFontRegistered();

            auto it = namedFonts_.find(name);
            if (it == namedFonts_.end() && !name.empty()) {
                // 登録が無い名前は、フォントフォルダのファイル → システムフォント
                // の順に解釈してみる。エディタで直接打ち込んだ名前を
                // そのまま使えるようにするため
                MsdfFontDesc probe = MakeDefaultDesc();

                const std::filesystem::path fontFile = ResolveFontFile(name);
                if (!fontFile.empty()) {
                    // ファイルを先頭に据える。後ろの既定フォールバック列は残しておく。
                    // 装飾系のフォントは漢字を持たないことが多く、
                    // 落とすと出ない字が □ になってしまうため
                    probe.filePath = fontFile.wstring();
                    namedFonts_[name] = probe;
                    it = namedFonts_.find(name);

                    Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                        "FontManager: フォントファイルとして登録しました: {}", name);
                }
                else {
                    const std::wstring familyName =
                        Logger::GetInstance().Utf8ToPath(name).wstring();

                    DirectWriteFontFace face;
                    if (face.LoadFromSystem(familyName)) {
                        // 見つかったファミリを先頭へ。後ろは既定のフォールバック列を残す
                        probe.systemFamilyNames.insert(
                            probe.systemFamilyNames.begin(), familyName);
                        namedFonts_[name] = probe;
                        it = namedFonts_.find(name);

                        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                            "FontManager: システムフォントとして登録しました: {}", name);
                    }
                }
            }

            if (it == namedFonts_.end()) {
                if (!name.empty() && name != kDefaultFontName) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "FontManager: フォント \"{}\" が見つかりません"
                        "（{} 内のファイル名か、インストール済みのフォント名を指定してください）。"
                        "既定フォントで代用します",
                        name, Logger::GetInstance().PathToUtf8(GetFontDirectory()));
                }
                it = namedFonts_.find(kDefaultFontName);
                if (it == namedFonts_.end()) { return nullptr; }
            }
            desc = it->second;
        }

        // Acquire は自前で mutex_ を取るので、ここでは持たずに呼ぶ
        return Acquire(desc);
    }

    const std::filesystem::path& FontManager::GetFontDirectory()
    {
        // シェーダ等と同じく作業ディレクトリからの相対で持つ。
        // ビルド後コピーで exe の隣にも同じ構成が置かれるので、
        // VS からの実行でも exe 単体の実行でも同じパスで引ける
        static const std::filesystem::path kDirectory = "Engine/Assets/Font";
        return kDirectory;
    }

    std::filesystem::path FontManager::ResolveFontFile(const std::string& name)
    {
        if (name.empty()) { return {}; }

        // フォルダの外を指せないよう、ファイル名だけを取り出して繋ぐ
        const std::filesystem::path requested =
            Logger::GetInstance().Utf8ToPath(name).filename();
        if (requested.empty()) { return {}; }

        const std::filesystem::path& directory = GetFontDirectory();
        std::error_code ec;

        // 拡張子まで書かれている指定（"851Gkktt_005.ttf"）
        const std::filesystem::path direct = directory / requested;
        if (IsFontFileExtension(direct.extension())
            && std::filesystem::is_regular_file(direct, ec)) {
            return direct;
        }

        // 拡張子を省いた指定（"851Gkktt_005"）にも合わせる
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file(ec)) { continue; }
            if (!IsFontFileExtension(entry.path().extension())) { continue; }
            if (entry.path().stem() == requested) { return entry.path(); }
        }
        return {};
    }

    std::vector<std::string> FontManager::GetFontFileNames() const
    {
        std::vector<std::string> names;

        // フォルダが無い場合は ec が立って end() と等しくなるので、そのまま空で返る
        std::error_code ec;
        for (const auto& entry :
            std::filesystem::directory_iterator(GetFontDirectory(), ec)) {
            if (!entry.is_regular_file(ec)) { continue; }
            if (!IsFontFileExtension(entry.path().extension())) { continue; }
            names.push_back(Logger::GetInstance().PathToUtf8(entry.path().filename()));
        }

        std::sort(names.begin(), names.end());
        return names;
    }

    std::vector<std::string> FontManager::GetSelectableFontNames() const
    {
        std::vector<std::string> names = GetRegisteredFontNames();

        // 一度使ったファイルは登録済みにもなるので、混ぜたあとで重複を落とす
        for (std::string& fileName : GetFontFileNames()) {
            names.push_back(std::move(fileName));
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    std::vector<std::string> FontManager::GetRegisteredFontNames() const
    {
        std::lock_guard lock(mutex_);
        std::vector<std::string> names;
        names.reserve(namedFonts_.size());
        for (const auto& [name, desc] : namedFonts_) {
            names.push_back(name);
        }
        return names;
    }

    void FontManager::Finalize()
    {
        std::lock_guard lock(mutex_);
        // フォントのデストラクタが自分のベイクタスクを待つので、
        // プールを畳むのはフォントを全て捨ててから
        fonts_.clear();
        namedFonts_.clear();
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
