#pragma once
#ifdef USE_IMGUI

#include "Input/InputAction.h"
#include <string>

namespace CoreEngine {

    class InputQuery;

    /// @brief キーコンフィグ ImGui ウィンドウ
    class KeyConfigUI {
    public:
        /// @brief キーコンフィグ画面を描画する
        /// @param query 編集対象の InputQuery
        void Draw(InputQuery& query);

    private:
        bool isListening_ = false;
        InputAction listeningAction_ = InputAction::Count;
        int listeningIndex_ = -1;
        std::string configFilePath_ = "Config/keybindings.json";
    };

}

#endif // USE_IMGUI
