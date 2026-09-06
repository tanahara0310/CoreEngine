#pragma once
#ifdef USE_IMGUI

#include "Input/InputAction.h"
#include "Input/InputBinding.h"
#include "Input/InputConfig.h"
#include <string>
#include <vector>

namespace CoreEngine {

    class InputQuery;

    /// @brief キーコンフィグ ImGui ウィンドウ
    class KeyConfigUI {
    public:
        /// @brief キーコンフィグ画面を描画する
        /// @param query 編集対象の InputQuery
        void Draw(InputQuery& query);

    private:
        /// @brief 入力待ち状態を解除する
        void StopListening();

        /// @brief バインディングを差し替えて即座にファイルへ書き出す
        void ApplyBindings(InputConfig& config, InputAction action, std::vector<InputBinding> bindings);

        /// @brief 現在の設定をファイルへ書き出し、成否を保持する
        void Save(const InputConfig& config);

        bool isListening_ = false;
        InputAction listeningAction_ = InputAction::Count;
        int listeningIndex_ = -1;
        bool saveFailed_ = false;   ///< 直近の自動保存に失敗したか（画面に警告を出すため）
        std::string configFilePath_{ InputConfig::kDefaultFilePath };
    };

}

#endif // USE_IMGUI
