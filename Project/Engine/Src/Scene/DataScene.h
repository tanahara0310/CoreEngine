#pragma once

#include "Scene/BaseScene.h"

#include <string>

namespace CoreEngine
{
    /// @brief シーンの保存データ（Application/Assets/Scenes/シーン名）だけでオブジェクトを組むシーン
    /// @details 動きはオブジェクトに付けたコンポーネント（スクリプトを含む）が受け持つ
    class DataScene final : public BaseScene
    {
    public:
        /// @param sceneName シーン名。保存データのフォルダ名にもなる
        explicit DataScene(std::string sceneName);

    protected:
        void OnInitialize() override;

    private:
        std::string sceneName_;
    };
}
