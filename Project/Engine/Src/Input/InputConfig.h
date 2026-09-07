#pragma once
#include "InputAction.h"
#include "InputBinding.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>

namespace CoreEngine {

    /// @brief アクション↔バインディングのマッピングを管理するクラス
    class InputConfig {
    public:
        /// @brief キーコンフィグの既定の保存先（実行時カレント基準）
        /// 起動時の自動読み込みと ImGui のキーコンフィグ画面が同じファイルを指すための共有定数
        static constexpr std::string_view kDefaultFilePath = "Application/Assets/Config/keybindings.json";

        /// @brief アクションのバインディングを一括設定
        void SetBindings(InputAction action, std::vector<InputBinding> bindings);

        /// @brief アクションにバインディングを追加
        void AddBinding(InputAction action, const InputBinding& binding);

        /// @brief アクションのバインディングをすべて削除
        void ClearBindings(InputAction action);

        /// @brief アクションのバインディング一覧を取得
        const std::vector<InputBinding>& GetBindings(InputAction action) const;

        /// @brief デフォルトのバインディングに戻す
        void ResetToDefault();

        /// @brief JSON ファイルからバインディングを読み込む
        /// 既定値を土台にファイルの内容を上書きする（ファイルに無いアクションは既定値のまま残る）
        /// @return 読み込みに成功した場合 true
        bool LoadFromFile(const std::string& filePath);

        /// @brief JSON ファイルにバインディングを保存する
        /// @return 保存に成功した場合 true
        bool SaveToFile(const std::string& filePath) const;

    private:
        /// @brief uint32_t をキーにしてハッシュ特殊化を不要にする
        std::unordered_map<uint32_t, std::vector<InputBinding>> bindings_;

        static const std::vector<InputBinding> kEmpty_;
    };

}
