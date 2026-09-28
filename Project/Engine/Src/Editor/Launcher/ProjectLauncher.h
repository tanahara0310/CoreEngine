#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
    class WinApp;
    struct EngineConfig;

    namespace Editor
    {
        /// @brief エンジンの初期化の前に出す、開くプロジェクトを選ぶ画面
        /// @details 窓・DirectX12・ImGui だけを用意して描く。プロジェクトが決まったら
        ///          ProjectPaths に設定し、窓を隠して戻る（そのままエンジンの初期化へ進む）。
        class ProjectLauncher
        {
        public:
            /// @brief 開くプロジェクトを決める
            /// @details 「次から前回のプロジェクトを自動で開く」が有効で前回のものがあれば、画面を出さずに決める。
            ///          起動の引数に `--launcher` があれば、その設定に関係なく画面を出す。
            /// @return 決まったら true。画面を閉じられたら false
            static bool Run(WinApp& winApp, const EngineConfig& config);
        };
    }
}

#endif // CORE_EDITOR
