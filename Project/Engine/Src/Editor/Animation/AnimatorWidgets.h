#pragma once

#ifdef CORE_EDITOR

#include "Animation/AnimatorController.h"

#include <imgui.h>

#include <optional>
#include <string>

namespace CoreEngine::Editor
{
    struct AnimatorDocument;
}

/// @brief アニメーターの窓の左・中・右で使う部品（種類と比べ方の文字・色、名前・モデル・クリップの欄）
namespace CoreEngine::Editor::AnimatorWidgets
{
    /// @brief 左の表のクリップをキャンバスへドラッグするときの名前（中身はクリップの名前）
    inline constexpr const char* kClipPayload = "ANIMATOR_CLIP";

    /// @brief 比べ方を並べる順
    inline constexpr AnimatorConditionOp kAllOps[] = {
        AnimatorConditionOp::Greater, AnimatorConditionOp::Less, AnimatorConditionOp::Equal,
        AnimatorConditionOp::NotEqual, AnimatorConditionOp::True, AnimatorConditionOp::False,
        AnimatorConditionOp::Trigger,
    };

    /// @brief パラメータの種類の 1 文字
    const char* TypeGlyph(AnimatorParameterType type);

    /// @brief パラメータの種類の名前
    const char* TypeLabel(AnimatorParameterType type);

    /// @brief パラメータの種類の色
    ImVec4 TypeColor(AnimatorParameterType type);

    /// @brief 比べ方の文字
    const char* OpLabel(AnimatorConditionOp op);

    /// @brief 比べる値の欄が要る比べ方か
    bool OpHasValue(AnimatorConditionOp op);

    /// @brief その種類で使える最初の比べ方
    AnimatorConditionOp FirstOp(AnimatorParameterType type);

    /// @brief 遷移の条件と終わりの時刻を 1 行にした文
    std::string TransitionSummary(const AnimatorTransition& transition, const AnimatorControllerData& data);

    /// @brief 名前の欄（書き終えて中身が変わっていたら true を返し、committed に入れる）
    bool NameField(AnimatorDocument& document, const char* label, const std::string& current, std::string& committed);

    /// @brief モデルのファイルを落とせる欄
    /// @return 変わったら true
    bool ModelField(const char* label, std::string& assetPath);

    /// @brief ドラッグで落とされたモデル（`Application/Assets/…` の綴り。落とされていなければ空）
    std::optional<std::string> AcceptModelDrop();

    /// @brief クリップの名前・ファイル・中の名前の欄
    /// @return クリップを消したら true
    bool ClipFields(AnimatorDocument& document, int clipIndex, bool allowDelete);
}

#endif // CORE_EDITOR
