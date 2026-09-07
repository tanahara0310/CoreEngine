#pragma once

#include "Text/MsdfFont.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class GraphicsCore;
    class ThreadPool;

    /// @brief MSDF フォントの所有と共有を一元管理するエンジンサービス
    /// @details
    ///  シーンが `MsdfFont` を直接 `unique_ptr` で持つと、
    ///   - シーンをまたぐたびにアトラスが焼き直される
    ///   - 同じフォントを複数シーンで共有できない
    ///   - キャッシュの置き場所が決まらない
    ///  という問題が出るため、所有をエンジン側へ引き上げる。
    ///
    ///  `TextureManager` / `ModelManager` と同じ位置づけで、
    ///  `EngineSystem::GetService<FontManager>()` から引く。
    ///
    /// @note 返す `MsdfFont*` の寿命は FontManager が持つ。シーンは参照するだけ。
    class FontManager
    {
    public:
        FontManager() = default;
        ~FontManager();

        FontManager(const FontManager&) = delete;
        FontManager& operator=(const FontManager&) = delete;

        /// @brief 初期化
        /// @param graphicsCore デバイスとディスクリプタの供給元
        void Initialize(GraphicsCore* graphicsCore);

        /// @brief 全フォントを破棄する
        /// @note GPU の処理完了後に呼ぶこと（ディスクリプタを解放するため）
        void Finalize();

        /// @brief 既定フォントの名前
        /// @details シーン JSON に font 名が無い / 未登録のときはこれへ倒す
        static constexpr const char* kDefaultFontName = "Default";

        /// @brief フォントに名前を付けて登録する
        /// @details
        ///  シーン JSON へは **名前だけ** を書き、実体はここから引く。
        ///  生成指定を丸ごと書き出すと、テキストごとに別アトラスが焼かれかねないうえ、
        ///  フォントを差し替えるたびに全シーンの JSON を書き換える羽目になる。
        /// @param name 参照名（"Default" / "Title" など）
        /// @param desc 生成指定
        void RegisterNamedFont(const std::string& name, const MsdfFontDesc& desc);

        /// @brief 名前でフォントを取得する
        /// @details
        ///  解決の順番は
        ///   ①登録済みの名前
        ///   → ②**フォントフォルダ内のファイル名**（GetFontDirectory）
        ///   → ③**システムフォントのファミリ名**
        ///   → ④既定フォント。
        ///  ②③があるので、エディタで "851Gkktt_005.ttf" や "Meiryo" のように
        ///  直接打ち込んだ名前がそのまま使える。
        ///  成功した名前はその場で登録されるので、シーンを保存して開き直しても同じ解決になる。
        MsdfFont* AcquireNamed(const std::string& name);

        /// @brief 登録済みの名前を列挙する（エディタのプルダウン用）
        std::vector<std::string> GetRegisteredFontNames() const;

        /// @brief エンジンが同梱するフォントファイルの置き場所
        /// @details 作業ディレクトリからの相対。他の Engine/Assets/... と同じ扱いで、
        ///          ここへ .ttf などを置けばエディタの一覧に出る
        static const std::filesystem::path& GetFontDirectory();

        /// @brief フォントフォルダに入っているフォントファイル名の一覧
        /// @return 拡張子付きのファイル名（例 "851Gkktt_005.ttf"）。
        ///         そのまま AcquireNamed / UIText::SetFontByName へ渡せる
        std::vector<std::string> GetFontFileNames() const;

        /// @brief エディタのフォント選択に出す名前の一覧
        /// @details 登録済みフォント名とフォントフォルダのファイル名を混ぜ、
        ///          重複を除いて並べたもの
        std::vector<std::string> GetSelectableFontNames() const;

        /// @brief フォントを取得する（同じ指定なら同じインスタンスを返す）
        /// @param desc 生成指定
        /// @return 生成済みフォント。失敗したら nullptr
        /// @details 初回だけ実際に構築する。2 回目以降は生成済みのものを返すので、
        ///          シーンの初期化で気軽に呼んでよい。
        MsdfFont* Acquire(const MsdfFontDesc& desc);

        /// @brief 保持しているフォント数（デバッグ表示用）
        size_t GetFontCount() const;

    private:
        /// @brief 生成指定から「同じ要求か」を判定するためのハッシュを作る
        /// @note ディスクキャッシュのキー（MsdfFontCache::ComputeKey）とは別物。
        ///       あちらは *実際に開けたフォント* を含むが、こちらは要求そのものを見る
        static uint64_t ComputeRequestHash(const MsdfFontDesc& desc);

        /// @brief 既定フォントが未登録なら、和文が出る最低限の指定で登録する
        void EnsureDefaultFontRegistered();

        /// @brief 既定の生成指定（焼き解像度・文字集合など）を返す
        /// @details 任意のフォント名を解決するときの土台に使う
        static MsdfFontDesc MakeDefaultDesc();

        /// @brief 名前をフォントフォルダ内のファイルとして解決する
        /// @param name 拡張子ありでも無しでもよい（"A.ttf" / "A"）
        /// @return 見つかったパス。無ければ空。フォルダの外は指せない
        static std::filesystem::path ResolveFontFile(const std::string& name);

        GraphicsCore* graphicsCore_ = nullptr;
        std::unordered_map<uint64_t, std::unique_ptr<MsdfFont>> fonts_;
        /// 名前 → 生成指定。実体は fonts_ 側にキャッシュされる
        std::unordered_map<std::string, MsdfFontDesc> namedFonts_;
        mutable std::mutex mutex_;

        /// @brief 実行時グリフのベイクを回すワーカー
        /// @details
        ///  MsdfFont はバッチ内のグリフをこのプールへ並列に投げる。
        ///  1 本はキューの取りまとめに使われるので、実際に焼くのは残り。
        ///  距離場の計算は CPU を食うため、増やしすぎると描画と食い合う。
        std::unique_ptr<ThreadPool> bakeThreadPool_;

        /// @brief ベイク用ワーカー数
        static constexpr uint32_t kBakeThreadCount = 4;
    };
}
