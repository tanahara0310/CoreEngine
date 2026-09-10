#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "UI/UIAnchor.h"

#include <functional>
#include <string>

namespace CoreEngine
{
    class UIImage;
    class UIText;
}

namespace ResultSceneUi
{
    using TextFactory = std::function<CoreEngine::UIText*(
        const std::string& text,
        float fontSize,
        CoreEngine::UIAnchor anchor,
        const CoreEngine::Vector2& anchoredPosition,
        const CoreEngine::Vector4& color,
        const std::string& name)>;

    using ImageFactory = std::function<CoreEngine::UIImage*(
        const std::string& texturePath,
        const std::string& name)>;

    /// @brief シーンが握っておく必要のある UI だけを返す。
    /// @details 板・ツタ・レール・トロッコといった中身は
    ///          `GameComponents::ResultGaugeUIComponent` が丸ごと持つ。
    ///          ここに出てくるのは、シーンが直接動かすもの
    ///          （選択肢の文字と Tips）だけに絞ってある。
    struct Elements
    {
        CoreEngine::UIImage* root = nullptr;      ///< ゲージ一式の入れ物
        CoreEngine::UIText* tipText = nullptr;
        CoreEngine::UIText* retryButton = nullptr;
        CoreEngine::UIText* titleButton = nullptr;
    };

    /// @brief リザルトの UI を組み立てる。
    /// @param createText  未使用（文字はドットフォントで組むため中で作る）。互換のため残す
    /// @param createImage 入れ物になる UIImage を 1 つ作る
    /// @details 見た目の調整は CVar `Result.Goal.*` / `Result.Gauge.*`
    ///          （インスペクターの「リザルトゲージ」）で行う。
    Elements Build(const TextFactory& createText, const ImageFactory& createImage = {});

    /// @brief 復元済みのTipsを、シーン構築後に表示へ反映する。
    void SetTipText(Elements& elements, const std::string& tip);
}
