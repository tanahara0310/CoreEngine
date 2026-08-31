#pragma once

#include "Scene/BaseScene.h"

#include <memory>
#include <vector>

namespace CoreEngine {
    class MsdfFont;
    class UIText;
}

namespace MsdfTextTest
{
    /// @brief MSDF フォント描画の検証シーン
    /// @details
    ///  「フォントファイル → MSDF アトラス → GPU 描画」が通っているかを目で見て確かめる。
    ///
    ///  検証したいのは 2 点:
    ///   ① DirectWrite → msdfgen::Shape 変換
    ///      → Cache/FontCache/ に吐かれるアトラス PNG を開いて、
    ///        グリフが正しい向き・形・色（MSDF 特有の赤緑青）で焼けているか確認する
    ///   ② HLSL の MSDF 描画 + fwidth によるアンチエイリアス
    ///      → 1 枚のアトラスから 12px と 96px を同時に出し、
    ///        さらに拡大縮小アニメーションで輪郭が崩れないことを確認する
    class MsdfTextTestScene : public CoreEngine::BaseScene {
    public:
        MsdfTextTestScene();
        /// @note MsdfFont を前方宣言で持つため、デストラクタは .cpp 側で定義する
        ~MsdfTextTestScene() override;

        void OnInitialize() override;

    protected:
        void OnUpdate() override;

    private:
        /// @brief 検証用テキストを 1 つ作って登録する
        CoreEngine::UIText* CreateText(
            const std::string& text,
            float fontSize,
            const CoreEngine::Vector2& position,
            const CoreEngine::Vector4& color,
            const std::string& name);

        /// @brief FontManager からフォントを取得する（失敗したらシーンはテキスト無しで動く）
        void BuildFont();

        /// @brief 使用するフォント（所有は FontManager。シーンは参照するだけ）
        CoreEngine::MsdfFont* font_ = nullptr;

        /// 拡大縮小で輪郭が崩れないことを見るための対象
        CoreEngine::UIText* scalingText_ = nullptr;
        /// 回転しても崩れないことを見るための対象
        CoreEngine::UIText* rotatingText_ = nullptr;
        /// 毎フレーム文字列が変わる対象（頂点バッファのフレーム跨ぎ破損の検証）
        CoreEngine::UIText* counterText_ = nullptr;
        /// アトラスに焼いていない文字を出す対象（実行時ベイクの検証）
        CoreEngine::UIText* dynamicText_ = nullptr;
        /// アトラスの登録数・待ち件数・使用量を出す
        CoreEngine::UIText* statusText_ = nullptr;
        /// バッチングの効き（ドローコール数）を出す
        CoreEngine::UIText* batchText_ = nullptr;

        float elapsedSeconds_ = 0.0f;
        uint64_t frameCount_ = 0;
        float dynamicTimer_ = 0.0f;
        size_t dynamicIndex_ = 0;
    };
}
