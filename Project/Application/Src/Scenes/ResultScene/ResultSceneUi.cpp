#include "pch.h"
#include "ResultSceneUi.h"

#include "Components/UI/ResultGaugeUIComponent.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"

namespace ResultSceneUi
{
    using namespace CoreEngine;

    namespace
    {
        /// 入れ物にするだけの 1x1 透明画像（ポーズメニューの暗幕と同じ版下）。
        /// @note Models/Box/white1x1.png はアルファチャンネルを持たないので使えない
        constexpr const char* kRootTexture = "Application/Assets/Textures/Pause/dim.png";
    }

    // リザルトの見た目は ResultGaugeUIComponent が丸ごと持つ。
    // ここは入れ物を 1 つ作って、シーンが直接動かす UI だけを取り出す層。
    Elements Build(const TextFactory& createText, const ImageFactory& createImage)
    {
        // 文字はドット絵のフォントで組む必要があり、シーンの既定フォントでは足りない。
        // そのため createText は使わないが、呼び出し側の形は変えずに残してある
        (void)createText;

        Elements elements;
        if (!createImage) {
            return elements;
        }

        auto* root = createImage(kRootTexture, "ResultGaugeRoot");
        if (!root) {
            return elements;
        }
        root->SetSerializeEnabled(false);
        root->SetAnchor(UIAnchor::TopCenter);
        root->SetPivot({ 0.5f, 0.5f });
        root->SetSize({ 1.0f, 1.0f });
        root->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        elements.root = root;

        // AddComponent の時点で Awake が走り、板・ツタ・レール・文字が揃う
        auto* gauge = root->AddComponent<GameComponents::ResultGaugeUIComponent>();
        if (!gauge) {
            return elements;
        }
        elements.retryButton = gauge->GetRetryText();
        elements.titleButton = gauge->GetTitleText();
        elements.tipText = gauge->GetTipText();
        return elements;
    }

    void SetTipText(Elements& elements, const std::string& tip)
    {
        if (!elements.tipText) {
            return;
        }
        elements.tipText->SetText(tip);
        elements.tipText->SetActive(!tip.empty());
    }
}
