#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine::Editor
{
    /// @brief エディタで最後に開いていたシーンを、自分だけの状態（`Application/Saved`）に控える
    namespace LastOpenedScene
    {
        /// @brief 控えたシーンの名前（控えが無ければ空）
        std::string Load();

        /// @brief シーンの名前を控える（控えと同じなら書かない）
        void Save(const std::string& sceneName);
    }
}

#endif // CORE_EDITOR
