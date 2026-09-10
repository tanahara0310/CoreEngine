#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "UI/UIAnchor.h"

#include <functional>
#include <string>

namespace CoreEngine
{
    class UIText;
}

namespace TitleSceneUi
{
    /// @brief UIText を生成するための関数型
    using TextFactory = std::function<CoreEngine::UIText*(
        const std::string& text,
        float fontSize,
        CoreEngine::UIAnchor anchor,
        const CoreEngine::Vector2& anchoredPosition,
        const CoreEngine::Vector4& color,
        const std::string& name)>;

    /// @brief タイトル UI のうち、シーン遷移で参照する要素
    struct Elements
    {
        CoreEngine::UIText* startHint = nullptr;
    };

    /// @brief タイトル画面の操作誘導テキストを生成する
    /// @param createText UIText の生成関数
    /// @param gamepadConnected 初期表示する操作デバイス
    /// @param createIntroCompletionCallback イントロ完了通知を登録する関数
    /// @return シーン本体が保持する必要のある UI ポインタ
    /// @details ボタン画像やクリック領域は作らず、操作方法を示すテキストだけを表示する。
    ///          初期値は Title.* CVar から与え、生成された UIText はシーン保存対象にする。
    ///          そのため、UIText インスペクターで変更したフォント・サイズ・位置を
    ///          StartHint.json として保持できる。TitleScene.cpp にはシーン遷移と
    ///          入力の流れだけを残す。
    using IntroCompletionCallbackFactory = std::function<std::function<void()>()>;

    Elements Build(
        const TextFactory& createText,
        bool gamepadConnected,
        const IntroCompletionCallbackFactory& createIntroCompletionCallback = {});

    /// @brief 接続中の入力デバイスに合わせて操作ヒントを更新する
    /// @param startHint 画面下部に表示するヒント用 UIText
    /// @param gamepadConnected true ならAボタン、false ならSPACEキーを表示
    void UpdateStartPrompt(CoreEngine::UIText* startHint, bool gamepadConnected);
}
