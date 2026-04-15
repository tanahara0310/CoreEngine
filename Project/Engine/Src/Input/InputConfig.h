#pragma once
#include "InputAction.h"
#include "InputBinding.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace CoreEngine {

    /// @brief アクション↔バインディングのマッピングを管理するクラス
    class InputConfig {
    public:
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
