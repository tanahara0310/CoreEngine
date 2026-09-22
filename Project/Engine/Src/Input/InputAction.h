#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine {

    /// @brief 論理入力アクション識別子
    /// @details 値は添字。名前と既定の割り当てはプロジェクト設定
    ///          （`Application/Config/EngineSettings/InputActions.json`）が持つので、
    ///          アクションを増やすのにエンジンの再ビルドは要らない。
    /// @note エンジンはアクションを名指ししない（数だけ見て回す）。
    ///       実体は `static_cast<InputAction>(添字)` で作る。
    enum class InputAction : uint32_t {
        Invalid = 0xFFFFFFFFu,  ///< 見つからなかったときの値
    };

    /// @brief 持てるアクションの上限
    inline constexpr std::size_t kMaxInputActions = 64;

    /// @brief アクション 1 つ分の定義
    struct InputActionDef {
        std::string id;                    ///< 保存データとスクリプトが使う綴り
        std::string displayName;           ///< キーコンフィグ画面に出す名前
        std::vector<std::string> defaults; ///< 既定の割り当て（"Key:W" などの綴り）
    };

    /// @brief アクションの定義の表
    /// @details 初回の参照で `InputActions.json` を読む。無ければ既定の並びを使う。
    namespace InputActions
    {
        /// @brief 使っているアクションの数（1 以上 kMaxInputActions 以下）
        std::size_t Count();

        /// @brief 定義の一覧（添字がそのままアクションの値）
        const std::vector<InputActionDef>& All();

        /// @brief 定義を決めて保存する
        /// @param defs 空・id の重複・上限超えは断る
        /// @param outError 断った訳（省略可）
        bool SetAll(std::vector<InputActionDef> defs, std::string* outError = nullptr);

        /// @brief ファイルから読み直す
        void Reload();
    }

    /// @brief アクションの識別文字列（範囲外なら "Unknown"）
    std::string_view InputActionToString(InputAction action);

    /// @brief アクションの表示名（範囲外なら "不明"）
    std::string_view InputActionToDisplayName(InputAction action);

    /// @brief 識別文字列からアクションを返す（不明なら Invalid）
    InputAction InputActionFromString(std::string_view str);
}
