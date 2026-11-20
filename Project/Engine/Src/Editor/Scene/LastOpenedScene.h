#pragma once

#ifdef CORE_EDITOR

#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief エディタで最後に開いていたシーンと、最近開いたシーンを、自分だけの状態（`Application/Saved`）に控える
    namespace LastOpenedScene
    {
        /// @brief 控えたシーンの名前（控えが無ければ空）
        std::string Load();

        /// @brief 最近開いたシーンの名前（新しい順・最大 10 個）
        std::vector<std::string> LoadRecent();

        /// @brief シーンの名前を控え、最近開いたシーンの先頭へ置く（控えと同じなら書かない）
        void Save(const std::string& sceneName);
    }
}

#endif // CORE_EDITOR
