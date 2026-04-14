#pragma once
#include <cstdint>
#include <string_view>

namespace CoreEngine {

    /// @brief 論理入力アクション識別子
    enum class InputAction : uint32_t {
        // 移動
        MoveForward = 0,
        MoveBack,
        MoveLeft,
        MoveRight,
        // アクション
        Jump,
        Attack,
        Interact,
        // UI
        UIConfirm,
        UICancel,
        // エディタ専用
        EditorGizmoTranslate,
        EditorGizmoRotate,
        EditorGizmoScale,

        Count ///< アクション総数（内部利用）
    };

    /// @brief アクションの識別文字列を返す（シリアライズ用）
    inline std::string_view InputActionToString(InputAction action) {
        switch (action) {
        case InputAction::MoveForward:          return "MoveForward";
        case InputAction::MoveBack:             return "MoveBack";
        case InputAction::MoveLeft:             return "MoveLeft";
        case InputAction::MoveRight:            return "MoveRight";
        case InputAction::Jump:                 return "Jump";
        case InputAction::Attack:               return "Attack";
        case InputAction::Interact:             return "Interact";
        case InputAction::UIConfirm:            return "UIConfirm";
        case InputAction::UICancel:             return "UICancel";
        case InputAction::EditorGizmoTranslate: return "EditorGizmoTranslate";
        case InputAction::EditorGizmoRotate:    return "EditorGizmoRotate";
        case InputAction::EditorGizmoScale:     return "EditorGizmoScale";
        default:                                return "Unknown";
        }
    }

    /// @brief アクションの日本語表示名を返す（UI 用）
    inline std::string_view InputActionToDisplayName(InputAction action) {
        switch (action) {
        case InputAction::MoveForward:          return "前進";
        case InputAction::MoveBack:             return "後退";
        case InputAction::MoveLeft:             return "左移動";
        case InputAction::MoveRight:            return "右移動";
        case InputAction::Jump:                 return "ジャンプ";
        case InputAction::Attack:               return "攻撃";
        case InputAction::Interact:             return "インタラクト";
        case InputAction::UIConfirm:            return "UI決定";
        case InputAction::UICancel:             return "UIキャンセル";
        case InputAction::EditorGizmoTranslate: return "ギズモ：移動";
        case InputAction::EditorGizmoRotate:    return "ギズモ：回転";
        case InputAction::EditorGizmoScale:     return "ギズモ：拡縮";
        default:                                return "不明";
        }
    }

    /// @brief 識別文字列からアクションを返す（不明な場合は Count を返す）
    inline InputAction InputActionFromString(std::string_view str) {
        if (str == "MoveForward")          return InputAction::MoveForward;
        if (str == "MoveBack")             return InputAction::MoveBack;
        if (str == "MoveLeft")             return InputAction::MoveLeft;
        if (str == "MoveRight")            return InputAction::MoveRight;
        if (str == "Jump")                 return InputAction::Jump;
        if (str == "Attack")               return InputAction::Attack;
        if (str == "Interact")             return InputAction::Interact;
        if (str == "UIConfirm")            return InputAction::UIConfirm;
        if (str == "UICancel")             return InputAction::UICancel;
        if (str == "EditorGizmoTranslate") return InputAction::EditorGizmoTranslate;
        if (str == "EditorGizmoRotate")    return InputAction::EditorGizmoRotate;
        if (str == "EditorGizmoScale")     return InputAction::EditorGizmoScale;
        return InputAction::Count;
    }

}
