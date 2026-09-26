#pragma once

#ifdef CORE_EDITOR

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

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
        std::atomic<int> copied{ 0 }; ///< 写し終えたファイルの数
        std::atomic<int> total{ 0 };  ///< 写すファイルの数
    };

    /// @brief 書き出しの結果
    struct ExportResult
    {
        std::string error;        ///< 失敗した訳（成功なら空）
        int fileCount = 0;        ///< 写したファイルの数
        std::uintmax_t bytes = 0; ///< 写した大きさ（バイト）
    };

    /// @brief 開いているプロジェクトを、Release の exe と一緒に書き出す
    /// @details 書き出す先の並びは Release の exe の隣と同じ
    ///          （exe と DLL・Engine/Assets・Application/Assets・Application/Config）。
    namespace GameExporter
    {
        /// @brief 何をどこへ写すかを決める
        ExportPlan Plan();

        /// @brief 写す（別のスレッドで呼んでよい）
        /// @details 書き出す先の Engine/Assets・Application/Assets・Application/Config は、消してから写す。
        ///          ほかのもの（ゲームが書く Application/Saved など）は残す。
        ExportResult Export(const ExportPlan& plan, ExportProgress& progress);
    }
}

#endif // CORE_EDITOR
