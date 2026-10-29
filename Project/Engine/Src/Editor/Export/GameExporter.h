#pragma once

#ifdef CORE_EDITOR

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief ゲームの書き出しで、何をどこへ写すか
    struct ExportPlan
    {
        std::filesystem::path releaseExe;  ///< Release でビルドした exe
        std::string releaseBuiltAt;        ///< その exe を作った日時（"2026-09-26 13:25"）
        std::filesystem::path destination; ///< 書き出す先（<プロジェクト>/Build/Windows）
        std::string error;                 ///< 書き出せない訳（書き出せるなら空）
    };

    /// @brief 書き出しの進み具合（書き出すスレッドが書き、描くスレッドが読む）
    struct ExportProgress
    {
        std::atomic<int> cooked{ 0 };    ///< 変換し終えた画像・モデル・シェーダ・JSON の数（失敗した物も数える）
        std::atomic<int> cookTotal{ 0 }; ///< 変換する画像・モデル・シェーダ・JSON の数
        std::atomic<int> copied{ 0 };    ///< 写し終えたファイルの数
        std::atomic<int> total{ 0 };     ///< 写すファイルの数
    };

    /// @brief 書き出しの結果
    struct ExportResult
    {
        std::string error;        ///< 失敗した訳（成功なら空）
        std::vector<std::string> failures; ///< 変換できなかったものの訳（全部集めてから止める）
        std::vector<std::string> warnings; ///< 書き出した後に確かめて見つかったこと（書き出しは済んでいる）
        int fileCount = 0;        ///< 書いたファイルの数
        int cookedTextures = 0;   ///< DDS へ変換した画像の数
        int cookedShaders = 0;    ///< DXIL へコンパイルしたシェーダの数
        int cookedModels = 0;     ///< .cmesh へ変換したモデルの数
        int convertedJson = 0;    ///< MessagePack へ変換した JSON のファイルの数
        std::uintmax_t bytes = 0; ///< 書いた大きさ（バイト）
    };

    /// @brief 開いているプロジェクトを、Release の exe と一緒に書き出す
    /// @details 書き出す先の並びは Release の exe の隣と同じ
    ///          （exe・DLL の Binaries・Engine/Assets・Application/Assets・Application/Config）。
    ///          DLL は Binaries にだけ置くので、書き出す先の根にある DLL は消す。
    ///          画像のテクスチャ（PNG・JPG など）は写さず、DDS へ変換して Engine/Cooked・Application/Cooked に書く。
    ///          モデルのファイル（と OBJ の .mtl・glTF の外部バッファ）も写さず、.cmesh へ変換して同じ場所に書く。
    ///          シェーダは一覧（ShaderManifest）のものを DXIL へコンパイルして同じ場所に書く（元の .hlsl も写す）。
    ///          中身が JSON のファイル（.json・.prefab・.mat・.meta・.physmat）は MessagePack へ変換して同じ名前で書く。
    ///          変換と写しはスレッドプールで並べて行う。変換できない物があれば全部集めてから、ファイルを写さずに止める。
    namespace GameExporter
    {
        /// @brief 何をどこへ写すかを決める
        ExportPlan Plan();

        /// @brief 書き出す（別のスレッドで呼んでよい）
        /// @details 書き出す先の Engine/Assets・Application/Assets・Application/Config・Engine/Cooked・
        ///          Application/Cooked は、消してから書く。ほかのもの（ゲームが書く Application/Saved など）は残す。
        ExportResult Export(const ExportPlan& plan, ExportProgress& progress);
    }
}

#endif // CORE_EDITOR
