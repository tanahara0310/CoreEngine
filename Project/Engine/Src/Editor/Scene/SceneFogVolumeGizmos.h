#pragma once

#ifdef CORE_EDITOR

#include "Utility/Lifetime/ScopedRegistration.h"

namespace CoreEngine::Editor
{
    struct SceneViewContext;

    /// @brief シーンビューに局所の霧（FogVolume）の形の輪郭を描く
    /// @details 有効な局所の霧を薄い線で、選んでいるもの（無効でも）を濃い線で描く。物に隠れた所は薄くする。
    class SceneFogVolumeGizmos
    {
    public:
        /// @brief シーンビューの道具へ登録する
        void Initialize();

    private:
        void Draw(const SceneViewContext& context);

        // シーンビューの道具の登録（破棄すると外れる）
        ScopedRegistration registration_;
    };
}

#endif // CORE_EDITOR
