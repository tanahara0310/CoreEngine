#include "pch.h"
#include "MsdfTextTestScene.h"

// BaseScene が unique_ptr で持つ型。デストラクタを .cpp 側で定義するため実体が要る
#include "Camera/CameraManager.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/TextRenderer.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <cmath>
#include <numbers>
#include <string>

namespace MsdfTextTest
{
    using namespace CoreEngine;

    namespace
    {
        /// 1x1 の白テクスチャ。scale がそのままピクセルサイズになるので矩形として使える
        constexpr const char* kWhiteTexture = "white1x1.png";

        // ── 検証に使う文字列 ──────────────────────────────────────
        // ソースは /utf-8 でコンパイルされるので、素の文字列リテラルが UTF-8 になる
        constexpr const char* kTitleText = "MSDF フォント描画テスト";
        constexpr const char* kLadderText = "あアA亜g国 0123 永";
        constexpr const char* kScalingText = "拡大テスト Ag亜";
        constexpr const char* kRotatingText = "回転しても鋭い";
        constexpr const char* kCornerText = "角が丸まらない：レ・ト・国・永";

        /// @brief U+E123（私用領域）の UTF-8 バイト列
        /// @note 文字列リテラルの \x エスケープは /utf-8 だと「ソース文字」と
        ///       解釈されて UTF-8 へ再エンコードされてしまう
        ///       （\xEE が î = C3 AE の 2 バイトになる）。
        ///       生バイトを置きたいときは char の配列で組むこと
        constexpr char kPrivateUseUtf8[] = { '\xEE', '\x84', '\xA3', '\0' };

        /// @brief 未収録文字の確認用の見出し
        /// @details U+E123 は私用領域なのでどのフォントも収録しない。
        ///          実行時ベイクでも焼けないので □ のまま残るのが正しい
        constexpr const char* kNotdefPrefix = "どのフォントにも無い文字 → ";
        constexpr const char* kNotdefSuffix = " (U+E123)";
        /// アトラスへ焼く分（U+E123 は当然含まれない）
        constexpr const char* kNotdefCharset = "どのフォントにも無い文字 → (U+E123)";

        /// 毎フレーム内容が変わるテキスト（頂点バッファの検証用）に使う文字
        constexpr const char* kCounterCharset = "経過秒フレーム目 ";

        /// @brief 実行時ベイクの確認用
        /// @details **これらの漢字は charsetUtf8 に入れない**。
        ///          最初の数フレームは □ が出て、裏で焼き上がると自動で本来の字へ変わる
        constexpr const char* kDynamicSamples[] = {
            "贅沢な麒麟が薔薇を頬張る",
            "檸檬・葡萄・林檎・蜜柑・西瓜",
            "躑躅 憂鬱 齷齪 灣 鑑 驫",
        };
        constexpr float kDynamicSwitchSeconds = 3.0f;

        /// 実行時ベイクの見出し（こちらは焼いておく）
        constexpr const char* kDynamicLabelCharset = "実行時ベイク 登録済み 待ち 使用量 枚 ％ 描画 回 文字";

        /// @brief 折り返し + 禁則処理の確認用
        /// @details 句読点・閉じ括弧・小書き仮名が行頭へ落ちない位置で折れているかを見る。
        ///          欧文は空白でのみ折れる（単語の途中で切らない）
        constexpr const char* kWrapText =
            "折り返しの確認。長い文章を指定した幅で折ります（禁則処理つき）。"
            "句読点や閉じ括弧、小書き仮名「っ」「ゃ」が行頭に落ちないこと。"
            " Latin words wrap at spaces only.";
        constexpr float kWrapWidthPx = 430.0f;

        /// 縁取りの見出し
        constexpr const char* kOutlineText = "縁取り Outline 亜";

        /// サイズ階段（px）。全て同じ 1 枚のアトラスから描かれる
        constexpr float kLadderSizes[] = { 12.0f, 16.0f, 22.0f, 30.0f, 42.0f, 58.0f };

        /// 拡大縮小アニメーションの範囲（px）
        constexpr float kScaleMinPx = 12.0f;
        constexpr float kScaleMaxPx = 100.0f;
        constexpr float kScalePeriodSeconds = 6.0f;

        /// フォントのフォールバック列（先頭が主フォント）
        /// @note 「最初に見つかった 1 つ」ではなく、文字ごとに先頭から探す。
        ///       先頭に無い文字は次のフォントから拾う
        const std::vector<std::wstring> kFontCandidates = {
            L"Yu Gothic UI",
            L"Meiryo",
            L"MS Gothic",
            L"Segoe UI",
        };

        /// アトラスの目視確認用 PNG の出力先（作業ディレクトリは Project/）
        constexpr const char* kAtlasDumpPath = "Cache/FontCache/MsdfTextTest_atlas.png";
    }

    MsdfTextTestScene::MsdfTextTestScene() = default;
    MsdfTextTestScene::~MsdfTextTestScene() = default;

    void MsdfTextTestScene::OnInitialize()
    {
        SetSceneName("MsdfTextTestScene");

        // 3D の床や空が映り込むと文字の輪郭が見づらいので止める
        SetDefaultGroundEnabled(false);

        BuildFont();

        // ── 背景（暗色。白文字のコントラストを取る）──────────────
        {
            auto* background = CreateObject<UIImage>();
            background->Initialize(kWhiteTexture, "Background");
            background->SetSerializeEnabled(false);
            background->SetAnchor(UIAnchor::Center);
            background->SetPivot({ 0.5f, 0.5f });
            background->SetAnchoredPosition({ 0.0f, 0.0f });
            background->SetSize({ 4096.0f, 4096.0f });
            background->SetColor({ 0.09f, 0.10f, 0.13f, 1.0f });
            background->SetSortOrder(-100);
        }

        if (!font_ || !font_->IsValid()) {
            // フォントが用意できなくてもシーンは起動する（背景だけ出る）
            return;
        }

        const Vector4 white = { 0.95f, 0.96f, 0.98f, 1.0f };
        const Vector4 accent = { 0.98f, 0.78f, 0.30f, 1.0f };
        const Vector4 dim = { 0.55f, 0.60f, 0.68f, 1.0f };

        // ── 見出し ────────────────────────────────────────────────
        CreateText(kTitleText, 34.0f, { 40.0f, 28.0f }, white, "Title");

        {
            // どのフォントが採用されたか・どう焼いたかを画面から読めるようにする
            const std::wstring& fontName = font_->GetResolvedFontName();
            const std::string info =
                "font: " + Logger::GetInstance().PathToUtf8(fontName)
                + "  /  atlas: " + std::to_string(static_cast<int>(font_->GetAtlasSize().x))
                + "x" + std::to_string(static_cast<int>(font_->GetAtlasSize().y))
                + "  /  pxRange: " + std::to_string(static_cast<int>(font_->GetPxRange()));
            CreateText(info, 15.0f, { 40.0f, 74.0f }, dim, "FontInfo");
        }

        // ── ①サイズ階段：1 枚のアトラスから 12px と 58px を同時に出す ──
        {
            float y = 118.0f;
            for (float size : kLadderSizes) {
                const std::string label =
                    std::string(kLadderText) + "  " + std::to_string(static_cast<int>(size)) + "px";
                CreateText(label, size, { 40.0f, y }, white, "Ladder");
                y += size + 14.0f;
            }
        }

        // ── ②コーナー保存の確認 ──────────────────────────────────
        // SDF なら丸まる字形（カタカナの角・漢字の交差）を並べる
        CreateText(kCornerText, 40.0f, { 40.0f, 430.0f }, accent, "CornerCheck");

        // ── ②-b 未収録文字の確認 ────────────────────────────────
        // U+E123 はどのフォントにも無い。黙って消えずに □ が出れば正しい
        CreateText(std::string(kNotdefPrefix) + kPrivateUseUtf8 + kNotdefSuffix,
            26.0f, { 40.0f, 490.0f }, dim, "NotdefCheck");

        // ── ②-c 毎フレーム文字列が変わるテキスト ────────────────
        // 頂点は UploadRing へ毎フレーム積み直すので、GPU が読んでいる最中の
        // 上書きが起きない。ちらつき・崩れが無ければ正しい
        counterText_ = CreateText("", 22.0f, { 40.0f, 530.0f }, white, "FrameCounter");

        // ── ②-d 実行時ベイク ────────────────────────────────────
        // アトラスに焼いていない漢字を出す。最初は □ で、裏で焼き上がると差し替わる
        CreateText("実行時ベイク", 18.0f, { 40.0f, 566.0f }, dim, "DynamicLabel");
        dynamicText_ = CreateText(kDynamicSamples[0], 32.0f, { 40.0f, 590.0f },
            accent, "DynamicText");
        statusText_ = CreateText("", 15.0f, { 40.0f, 636.0f }, dim, "AtlasStatus");
        batchText_ = CreateText("", 15.0f, { 40.0f, 658.0f }, accent, "BatchStatus");

        // ── ②-e 縁取り ──────────────────────────────────────────
        // 距離場のしきい値をずらすだけで作れる。ビットマップフォントなら
        // 縁取り用のアトラスを別に焼く必要がある
        {
            auto* plain = CreateText(kOutlineText, 44.0f, { 740.0f, 120.0f }, white, "OutlinePlain");
            if (plain) { plain->SetOutline({ 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f); }

            auto* outlined = CreateText(kOutlineText, 44.0f, { 740.0f, 180.0f }, accent, "OutlineThin");
            if (outlined) { outlined->SetOutline({ 0.05f, 0.05f, 0.08f, 1.0f }, 0.03f); }

            auto* thick = CreateText(kOutlineText, 44.0f, { 740.0f, 240.0f },
                { 0.35f, 0.85f, 0.95f, 1.0f }, "OutlineThick");
            if (thick) { thick->SetOutline({ 0.05f, 0.05f, 0.08f, 1.0f }, 0.06f); }

            auto* bold = CreateText(kOutlineText, 44.0f, { 740.0f, 300.0f }, white, "OutlineBold");
            if (bold) {
                bold->SetWeight(0.012f); // 太さもしきい値をずらすだけ
                bold->SetOutline({ 0.9f, 0.35f, 0.3f, 1.0f }, 0.045f);
            }
        }

        // ── ②-f 折り返し + 禁則処理 ────────────────────────────
        {
            auto* wrapped = CreateText(kWrapText, 20.0f, { 740.0f, 380.0f }, white, "WrapText");
            if (wrapped) { wrapped->SetWrapWidth(kWrapWidthPx); }
        }

        // ── ③拡大縮小アニメーション（要件の直接検証）──────────────
        scalingText_ = CreateText(
            kScalingText, kScaleMinPx, { 0.0f, 90.0f }, accent, "ScalingText");
        if (scalingText_) {
            scalingText_->SetAnchor(UIAnchor::Center);
            scalingText_->SetPivot({ 0.5f, 0.5f });
        }

        // ── ④回転しても崩れないことの確認 ────────────────────────
        rotatingText_ = CreateText(
            kRotatingText, 30.0f, { 0.0f, 220.0f }, white, "RotatingText");
        if (rotatingText_) {
            rotatingText_->SetAnchor(UIAnchor::Center);
            rotatingText_->SetPivot({ 0.5f, 0.5f });
        }

        // ── 見方の説明 ────────────────────────────────────────────
        {
            auto* hint = CreateText(
                "拡大しても輪郭が鋭いまま／縮小しても消えないことを確認する",
                16.0f, { 40.0f, -34.0f }, dim, "Hint");
            if (hint) {
                hint->SetAnchor(UIAnchor::BottomLeft);
                hint->SetPivot({ 0.0f, 0.0f });
            }
        }
    }

    void MsdfTextTestScene::BuildFont()
    {
        auto* fontManager = engine_ ? engine_->GetService<FontManager>() : nullptr;
        if (!fontManager) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfTextTestScene: FontManager を取得できませんでした");
            return;
        }

        MsdfFontDesc desc{};
        desc.systemFamilyNames = kFontCandidates;

        // 最小構成では「使う文字を全部先に焼く」方式。
        // 和文を本格対応する際は、ここを動的アトラス（出てきた文字だけ焼く）へ置き換える
        desc.charsetUtf8 =
            std::string(kTitleText) + kLadderText + kScalingText + kRotatingText + kCornerText
            + kNotdefCharset + kCounterCharset + kDynamicLabelCharset
            + kWrapText + kOutlineText
            + "px 拡大しても輪郭が鋭いまま／縮小しても消えないことを確認する"
            + "font atlas pxRange";
        desc.includeAscii = true;

        desc.bake.glyphPixelSize = 40; // 和文の推奨値
        // 縁取りの太さは距離場が持つ情報量（pxRange/2）で頭打ちになる。
        // 4 だと 0.05em までしか太らせられないので、縁取りを使うなら少し広げる。
        // 広げすぎると画数の多い漢字でストローク同士の距離場が干渉するため 6 で止める
        desc.bake.pxRange = 6.0f;
        // 1 枚あたりを小さくして、枚をまたぐ経路を実際に通す。
        // 総面積は 1024x1024 x1枚 と同じ（実運用では 1024x1024 x4枚 あたりが目安）
        desc.bake.atlasWidth = 512;
        desc.bake.atlasHeight = 512;
        desc.bake.atlasPageCount = 4;
        desc.bake.padding = 2;

        // 工程①（DirectWrite → Shape 変換）の目視確認用。
        // 出力された PNG を開いて、グリフが正しい向き・形で焼けているかを見る
        desc.debugAtlasDumpPath = kAtlasDumpPath;

        // 所有は FontManager。同じ指定なら再利用され、
        // 初回だけディスクキャッシュを見て、無ければ焼いて保存する
        font_ = fontManager->Acquire(desc);
        if (!font_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "MsdfTextTestScene: MSDF フォントの取得に失敗しました");
        }
    }

    UIText* MsdfTextTestScene::CreateText(
        const std::string& text,
        float fontSize,
        const Vector2& position,
        const Vector4& color,
        const std::string& name)
    {
        if (!font_) { return nullptr; }

        auto* uiText = CreateObject<UIText>();
        uiText->Initialize(font_, text, name);
        uiText->SetSerializeEnabled(false);
        uiText->SetAnchor(UIAnchor::TopLeft);
        uiText->SetPivot({ 0.0f, 0.0f });
        uiText->SetAnchoredPosition(position);
        uiText->SetFontSize(fontSize);
        uiText->SetColor(color);
        uiText->SetSortOrder(10);
        return uiText;
    }

    void MsdfTextTestScene::OnUpdate()
    {
        elapsedSeconds_ += Time::DeltaTime();
        ++frameCount_;

        // ── 毎フレーム文字列を差し替える ──────────────────────────
        // 自前の UPLOAD バッファを書き換える実装だと、GPU が前フレームの頂点を
        // 読んでいる最中に上書きしてここが崩れる。UploadRing 化の検証がこれ
        if (counterText_) {
            counterText_->SetText(
                "経過 " + std::to_string(static_cast<int>(elapsedSeconds_)) + " 秒 / "
                + std::to_string(frameCount_) + " フレーム目");
        }

        // ── 拡大縮小：0..1 を往復させてフォントサイズへ写す ────────
        // 頂点は em 単位なので、ここでサイズを変えても頂点は組み直されない。
        // それでも輪郭が鋭いままなのが MSDF の効果
        if (scalingText_) {
            const float phase =
                elapsedSeconds_ * 2.0f * static_cast<float>(std::numbers::pi) / kScalePeriodSeconds;
            const float t = 0.5f - 0.5f * std::cos(phase); // 0..1 を滑らかに往復
            scalingText_->SetFontSize(kScaleMinPx + (kScaleMaxPx - kScaleMinPx) * t);
        }

        // ── 実行時ベイク：一定間隔で未登録の文字列へ差し替える ────
        // 差し替えた直後は □ で出て、ワーカーが焼き終わると自動で本来の字になる
        dynamicTimer_ += Time::DeltaTime();
        if (dynamicText_ && dynamicTimer_ >= kDynamicSwitchSeconds) {
            dynamicTimer_ = 0.0f;
            dynamicIndex_ = (dynamicIndex_ + 1) % std::size(kDynamicSamples);
            dynamicText_->SetText(kDynamicSamples[dynamicIndex_]);
        }

        // アトラスの状態を毎フレーム出す（焼き進み具合が数字で追える）
        if (statusText_ && font_) {
            statusText_->SetText(
                "登録済み " + std::to_string(font_->GetGlyphCount())
                + " / 待ち " + std::to_string(font_->GetPendingGlyphCount())
                + " / 使用量 " + std::to_string(static_cast<int>(font_->GetAtlasOccupancy() * 100.0f))
                + " ％ / " + std::to_string(font_->GetUsedPageCount())
                + " / " + std::to_string(font_->GetPageCount()) + " 枚");
        }

        // バッチングの効きを画面に出す。
        // シーンには 20 個以上の UIText があるが、まとめて 1 回で描かれる
        if (batchText_ && engine_) {
            if (auto* renderManager = engine_->GetService<RenderManager>()) {
                auto* textRenderer = dynamic_cast<TextRenderer*>(
                    renderManager->GetRenderer(RenderPassType::UIText));
                if (textRenderer) {
                    batchText_->SetText(
                        "描画 " + std::to_string(textRenderer->GetLastFrameDrawCallCount())
                        + " 回 / " + std::to_string(textRenderer->GetLastFrameGlyphCount())
                        + " 文字");
                }
            }
        }

        // ── 回転：MSDF は回転にも強い（ビットマップ方式だと斜めでジャギる）──
        if (rotatingText_) {
            rotatingText_->SetUIRotation(std::sin(elapsedSeconds_ * 0.7f) * 0.35f);
        }
    }
}
