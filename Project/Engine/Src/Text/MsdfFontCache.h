#pragma once

#include "Text/MsdfFontBaker.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine
{
    struct MsdfFontDesc;

    /// @brief 焼き上がった MSDF アトラスをディスクへ残して再利用する
    /// @details
    ///  ベイクは 1 グリフあたり数 ms かかる（Release で 152 グリフ 0.66 秒、
    ///  最適化なしの Debug では 15 秒）。和文を数千字まで広げると
    ///  毎起動これを払うのは現実的でないので、結果を丸ごと保存する。
    ///
    ///  形式は独自のバイナリ。PNG / DDS を経由しないのは、
    ///  途中で圧縮やミップ生成が挟まると距離場が壊れるため
    ///  （非圧縮・ミップ無しであることを形式として保証したい）。
    class MsdfFontCache
    {
    public:
        /// @brief ディスクキャッシュのキーを求める
        /// @param desc 生成指定（焼き解像度・pxRange・文字集合など）
        /// @param resolvedChain 実際に開けたフォント名（先頭が主フォント）
        /// @details **実際に開けたフォント**を含めるのが要点。
        ///          指定だけで作ると、目的のフォントが入っていない環境で
        ///          別フォントのアトラスを掴んでしまう。
        static uint64_t ComputeKey(const MsdfFontDesc& desc,
            const std::vector<std::wstring>& resolvedChain);

        /// @brief キーからキャッシュファイルのパスを組み立てる
        static std::filesystem::path MakePath(const std::filesystem::path& directory, uint64_t key);

        /// @brief キャッシュを読み込む
        /// @return 読めたら true。壊れている・版が違う場合は false（焼き直しへ倒す）
        static bool TryLoad(const std::filesystem::path& path, MsdfBakeResult& outResult);

        /// @brief キャッシュを書き出す
        static bool Save(const std::filesystem::path& path, const MsdfBakeResult& result);
    };
}
