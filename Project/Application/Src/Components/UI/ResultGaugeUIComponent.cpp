#include "pch.h"
#include "ResultGaugeUIComponent.h"

#include "Audio/AudioSystem.h"
#include "Components/GameCore/GameRecordStore.h"
#include "Components/GameCore/GameResultData.h"
#include "Components/Result/ResultButtonAnimationComponent.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Math/Easing/EasingUtil.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Random/RandomGenerator.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif

#include <algorithm>
#include <cmath>

using namespace CoreEngine;

namespace
{
    // ───────────────────────────────────────────────────────────────
    // テクスチャ（すべて既存の流用。新規アセットは無い）
    //  Pause/*      … 板・ツタ・茂み・葉カーソル・舞う葉・暗幕・レール（ポーズメニューと同じ版下。
    //                 レールは板の中央パーツを引き伸ばして使う。専用のレール絵は
    //                 質感だけ写真っぽくて浮くので採用しない）
    //  result_cart  … トロッコ（loading_cart.png の絵を、UI と同じドット密度へ焼き直したもの。
    //                 元の版下は 1 ドットが画面 6.3px かつ非整数グリッドで、周りより粗くにじんでいた）
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTexPlankMid = "Application/Assets/Textures/Pause/plank_mid.png";
    constexpr const char* kTexPlankCapL = "Application/Assets/Textures/Pause/plank_cap_l.png";
    constexpr const char* kTexPlankCapR = "Application/Assets/Textures/Pause/plank_cap_r.png";
    constexpr const char* kTexVineV = "Application/Assets/Textures/Pause/vine_v.png";
    constexpr const char* kTexVineH = "Application/Assets/Textures/Pause/vine_h.png";
    constexpr const char* kTexFoliage = "Application/Assets/Textures/Pause/foliage.png";
    constexpr const char* kTexCursor = "Application/Assets/Textures/Pause/cursor.png";
    constexpr const char* kTexLeafM = "Application/Assets/Textures/Pause/leaf_m.png";
    constexpr const char* kTexLeafS = "Application/Assets/Textures/Pause/leaf_s.png";
    constexpr const char* kTexDim = "Application/Assets/Textures/Pause/dim.png";
    constexpr const char* kTexCart = "Application/Assets/Textures/result_cart.png";

    constexpr const char* kSeTick = "Application/Assets/Sounds/SE/rail_build.mp3";
    constexpr const char* kSeArrive = "Application/Assets/Sounds/SE/title_bound.mp3";
    constexpr const char* kSePassRecord = "Application/Assets/Sounds/SE/build.mp3";

    // ───────────────────────────────────────────────────────────────
    // テクスチャの実寸
    // ───────────────────────────────────────────────────────────────
    constexpr float kPlankHeight = 96.0f;
    constexpr float kCapWidth = 40.0f;
    constexpr float kVineWidth = 32.0f;
    constexpr float kVineHeight = 64.0f;
    constexpr float kCreeperWidth = 128.0f;
    constexpr float kCreeperHeight = 48.0f;
    constexpr float kFoliageWidth = 208.0f;
    constexpr float kFoliageHeight = 128.0f;
    constexpr float kCursorWidth = 128.0f;
    constexpr float kCursorHeight = 64.0f;
    constexpr float kLeafWidth = 48.0f;
    constexpr float kLeafHeight = 28.0f;

    // ───────────────────────────────────────────────────────────────
    // 配置（基準解像度 1920x1080。アンカーは TopCenter。x は画面中央から、y は上端から）
    //
    // 背景の「地面に埋まったサル」が主役なので、UI は上端・下端・四隅にだけ置く。
    // y = 250〜760 には何も出さない。ここを守るために各値は決め打ちにしてある。
    // ───────────────────────────────────────────────────────────────
    constexpr float kHeadTop = 44.0f;        ///< 見出し板の上端（ここから 2 段ぶんが空の中に収まる）
    constexpr float kHeadWidth = 780.0f;
    constexpr float kHeadRow1Y = kHeadTop + kPlankHeight * 0.5f;
    constexpr float kHeadRow2Y = kHeadTop + kPlankHeight * 1.5f;
    constexpr float kDistanceFontSize = 84.0f;
    constexpr float kRemainFontSize = 32.0f;
    constexpr float kHeadVineX[] = { -290.0f, -155.0f, 0.0f, 155.0f, 290.0f };

    constexpr float kSideCenterX = -644.0f;  ///< 左上の小さな板（画面 x=316）
    constexpr float kSideWidth = 452.0f;
    constexpr float kSideY = 92.0f;
    constexpr float kSideFontSize = 30.0f;
    constexpr float kRankY = 176.0f;
    constexpr float kRankHeight = 76.0f;
    constexpr float kRankFontSize = 26.0f;
    constexpr float kBestCenterX = 644.0f;   ///< 自己最高の板。左上のサル数板とちょうど左右対称
    constexpr float kBestY = kSideY;
    constexpr float kBestWidth = kSideWidth;
    constexpr float kBestHeight = kPlankHeight;
    constexpr float kBestFontSize = kSideFontSize;

    constexpr float kNewRecordX = 470.0f;    ///< 見出し板の右肩にぶら下げる「しんきろく！」
    constexpr float kNewRecordY = 206.0f;
    constexpr float kNewRecordWidth = 300.0f;
    constexpr float kNewRecordRotation = -0.13f;
    constexpr float kNewRecordFontSize = 40.0f;

    constexpr float kRailY = 800.0f;
    constexpr float kRailLeft = -760.0f;     ///< 0m（画面 x=200）
    constexpr float kGoalX = 600.0f;         ///< 目標地点（画面 x=1560）
    constexpr float kRailHeight = 44.0f;
    // 目盛りは 100m 刻み。枕木（レールの縞）はその 100m を等分した位置に置くので、
    // 何本目かを数えれば必ず目盛りの杭に行き当たる。px を直に刻むとここがずれる
    constexpr float kTickStepMeters = 100.0f;
    constexpr int kTiesPerTick = 5;          ///< 目盛り 1 つを枕木で等分する数（= 20m ごと）
    constexpr float kMinTiePitch = 24.0f;    ///< これより詰まるなら等分数を落とす
    constexpr int kMaxTicks = 16;
    constexpr float kSleeperWidth = 15.0f;
    constexpr float kSleeperHeight = 34.0f;
    constexpr float kRailVineStep = 96.0f;
    constexpr float kTickFontSize = 24.0f;
    constexpr float kTickLabelY = kRailY + 58.0f;
    // 100m の杭は枕木を下へ伸ばしたもの。太さと上端を枕木に合わせないと、
    // 並べたときに「太さが違う」「上が欠けて空いて見える」になる
    constexpr float kTickPostWidth = kSleeperWidth;
    constexpr float kTickPostHeight = 52.0f;
    constexpr float kTickPostY =
        kRailY - kSleeperHeight * 0.5f + kTickPostHeight * 0.5f;
    constexpr float kGateBeamY = 638.0f;
    constexpr float kGateBeamWidth = 350.0f;
    constexpr float kGatePostWidth = 40.0f;  ///< 看板を支える柱。まえのきろくの杭より太い
    constexpr float kGatePostTop = kGateBeamY + kPlankHeight * 0.5f;
    constexpr float kGateFontSize = 30.0f;
    // トロッコは result_cart.png（25x26 ドットを 10 倍で書き出した 250x260）。
    // 1 ドット = 画面 5px で置くと、ミップ 1 がちょうど表示サイズと一致してドットが崩れない。
    // 外周 1 ドットは透明なので、絵の見えている大きさは 115x120
    constexpr float kCartDot = 5.0f;
    constexpr float kCartWidth = kCartDot * 25.0f;
    constexpr float kCartHeight = kCartDot * 26.0f;
    // 最後の + kCartDot は、下端の透明 1 ドットぶんを詰めてレールへ乗せ直す補正
    constexpr float kCartY =
        kRailY - kRailHeight * 0.5f - kCartHeight * 0.5f + 6.0f + kCartDot;
    constexpr float kOverGoalMaxX = 46.0f;   ///< 目標を越えたぶんのはみ出し幅

    constexpr float kRecordPlankWidth = 300.0f;
    constexpr float kRecordPlankHeight = 52.0f;
    // 札はトロッコの上端（kCartY - kCartHeight * 0.5）より上へ置く。
    // 同じ位置に来たとき絵が重なってどちらも読めなくなるため
    constexpr float kRecordPlankY =
        kRailY - kRailHeight * 0.5f - kCartHeight + 6.0f + kCartDot
        - kRecordPlankHeight * 0.5f - 8.0f;
    constexpr float kRecordPostTop = kRecordPlankY + kRecordPlankHeight * 0.5f;
    constexpr float kRecordPostBottom = 806.0f;
    constexpr float kRecordPostWidth = 20.0f;
    constexpr float kRecordFontSize = 24.0f;

    constexpr float kChoiceY = 940.0f;
    constexpr float kChoiceOffsetX = 262.0f;
    constexpr float kChoiceWidth = 420.0f;
    constexpr float kChoiceFontSize = 44.0f;
    constexpr float kChoiceTilt = -0.030f;

    constexpr float kCanvasWidth = 1920.0f;  ///< 基準解像度の横幅（帯を端まで伸ばすのに使う）
    constexpr float kTipY = 1042.0f;
    constexpr float kTipFontSize = 34.0f;
    constexpr float kTipBgHeight = 64.0f;
    constexpr float kTipBgAlpha = 0.5f;      ///< 薄い黒帯。背景を隠しすぎない濃さ
    constexpr float kTipPulseSeconds = 1.6f; ///< Tips の明滅 1 周期
    // Tips の色（リニア 0.62）はとっくに飽和域なので、明るい側へ振っても画面は動かない。
    // 今の明るさを山にして、暗い側だけへ振ると「点滅」に見える
    constexpr float kTipPulseMin = 0.30f;

    // ───────────────────────────────────────────────────────────────
    // 演出
    // ───────────────────────────────────────────────────────────────
    constexpr float kOutlineWidth = 0.05f;
    constexpr float kSwaySpeed = 1.4f;
    constexpr float kSwayAngle = 0.012f;
    constexpr float kCursorFollow = 22.0f;
    constexpr float kCursorGap = 34.0f;   ///< 札の左端から葉カーソルまでの隙間
    constexpr float kLeafGravity = 900.0f;
    constexpr float kLeafFadeSeconds = 0.35f;
    constexpr float kMaxStepSeconds = 0.1f;
    constexpr float kExposureScaleMin = 0.02f;
    constexpr float kExposureScaleMax = 4.0f;
    constexpr float kSeTickVolume = 0.35f;
    constexpr float kSeArriveVolume = 0.7f;

    // ドット絵のフォント。焼く字は使う文言をそのまま並べてある
    // （文言を足したらここへも足すこと。動的グリフはあるが最初の数フレーム □ が出る）
    constexpr const char* kPixelCharset =
        "つれてきたサルひき"
        "もくひょうまであとｍ"
        "とっぱ！＋"
        "まえのきろく"
        "さいこうきろく"
        "しんきろく"
        "しょうごう"
        "はじめのいっぽ"
        "みならいレールこう"
        "かけだしのつなぎて"
        "ジャングルのあんないやく"
        "トロッコのたつじん"
        "でんせつのサル"
        "もういちど"
        "タイトルへ"
        "えらぶけってい"
        "← →";

    /// @brief 距離に応じた称号。目標距離に対する比で決めるので、目標を変えても崩れない
    const char* RankName(float ratio)
    {
        if (ratio >= 1.4f) { return "でんせつのサル"; }
        if (ratio >= 1.0f) { return "トロッコのたつじん"; }
        if (ratio >= 0.8f) { return "あと いっぽ"; }
        if (ratio >= 0.6f) { return "ジャングルのあんないやく"; }
        if (ratio >= 0.4f) { return "かけだしのつなぎて"; }
        if (ratio >= 0.2f) { return "みならいレールこう"; }
        return "はじめのいっぽ";
    }

    Vector2 RotateOffset(const Vector2& offset, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { offset.x * c - offset.y * s, offset.x * s + offset.y * c };
    }

    float Follow(float current, float target, float rate, float deltaTime)
    {
        return current + (target - current) * (1.0f - std::exp(-rate * deltaTime));
    }

    float Rand(float minimum, float maximum)
    {
        return RandomGenerator::GetInstance().GetFloat(minimum, maximum);
    }

    template <class T>
    void SetActiveIf(T* object, bool active)
    {
        if (object) {
            object->SetActive(active);
        }
    }
}

namespace GameComponents
{
    // 【色の入れ方】UI はトーンマップ前のバッファへ sRGB デコード無しで描かれる。
    // ここへ入れた値はリニア値として扱われ、ACES → sRGB で大きく持ち上がって画面に出る。
    // 既定値はポーズメニューと同じ「画面で出したい色から逆算した」値。
    CVar<float> ResultGaugeUIComponent::GoalMeters{
        "Result.Goal.Meters", 500.0f,
        "リザルトのゲージが示す目標距離 [m]。バランス調整で動かす想定",
        CVarRange{ 50.0f, 5000.0f } };

    CVar<float> ResultGaugeUIComponent::CountSeconds{
        "Result.Gauge.CountSeconds", 1.15f,
        "0m から今回の距離まで数えあげる秒数",
        CVarRange{ 0.0f, 5.0f } };

    CVar<float> ResultGaugeUIComponent::Brightness{
        "Result.Gauge.Brightness", 1.0f,
        "板とツタの明るさ。上げるとブルームで白茶ける",
        CVarRange{ 0.2f, 2.0f } };

    CVar<float> ResultGaugeUIComponent::RailBrightness{
        "Result.Gauge.RailBrightness", 0.32f,
        "トロッコの明るさ。loading_cart.png は見たままの色で描かれているので、"
        "等倍で貼ると白飛びする（レールは板と同じ Brightness を使うのでここでは動かない）",
        CVarRange{ 0.05f, 1.5f } };

    CVar<int> ResultGaugeUIComponent::SortOrder{
        "Result.Gauge.SortOrder", 1500,
        "リザルトゲージの描画順（大きいほど手前）",
        CVarRange{ 0.0f, 8000.0f } };

    CVar<bool> ResultGaugeUIComponent::ShowPreviousRecord{
        "Result.Gauge.ShowPreviousRecord", true,
        "前回の記録の杭をゲージ上に立てる" };

    CVar<Vector4> ResultGaugeUIComponent::NumberColor{
        "Result.Gauge.NumberColor", { 0.944f, 0.413f, 0.053f, 1.0f },
        "距離の数字の色。画面で (230,196,62) に見える値" };

    CVar<Vector4> ResultGaugeUIComponent::LabelColor{
        "Result.Gauge.LabelColor", { 0.735f, 0.546f, 0.296f, 1.0f },
        "見出し・木札の文字色。画面で (222,210,176) に見える値" };

    CVar<Vector4> ResultGaugeUIComponent::AccentColor{
        "Result.Gauge.AccentColor", { 1.000f, 0.546f, 0.087f, 1.0f },
        "とっぱ・しんきろくの文字色。画面で (232,210,90) に見える値" };
}

#ifdef USE_IMGUI
bool GameComponents::ResultGaugeUIComponent::DrawInspector()
{
    return CoreEngine::CVarUI::DrawTree("Result.Goal")
        | CoreEngine::CVarUI::DrawTree("Result.Gauge");
}
#endif

// ───────────────────────────────────────────────────────────────────
// 生成
// ───────────────────────────────────────────────────────────────────

UIImage* GameComponents::ResultGaugeUIComponent::SpawnImage(
    const char* texture, const std::string& name, int order)
{
    auto* owner = GetOwner();
    if (!owner) {
        return nullptr;
    }
    auto* image = owner->Spawn<UIImage>();
    if (!image) {
        return nullptr;
    }
    image->Initialize(texture, name);
    image->SetSerializeEnabled(false);
    image->SetAnchor(UIAnchor::TopCenter);
    image->SetPivot({ 0.5f, 0.5f });
    image->SetSortOrder(order);
    return image;
}

UIText* GameComponents::ResultGaugeUIComponent::SpawnText(
    MsdfFont* font, const std::string& text, const std::string& name,
    float fontSize, int order)
{
    auto* owner = GetOwner();
    if (!owner || !font) {
        return nullptr;
    }
    auto* label = owner->Spawn<UIText>();
    if (!label) {
        return nullptr;
    }
    label->Initialize(font, text, name);
    label->SetSerializeEnabled(false);
    label->SetAnchor(UIAnchor::TopCenter);
    label->SetPivot({ 0.5f, 0.5f });
    label->SetFontSize(fontSize);
    label->SetOutline({ 0.0f, 0.0f, 0.0f, 1.0f }, kOutlineWidth);
    label->SetSortOrder(order);
    return label;
}

GameComponents::ResultGaugeUIComponent::Plank
GameComponents::ResultGaugeUIComponent::SpawnPlank(const std::string& name, int order)
{
    Plank plank;
    plank.mid = SpawnImage(kTexPlankMid, name + "Mid", order);
    plank.capLeft = SpawnImage(kTexPlankCapL, name + "CapL", order + 1);
    plank.capRight = SpawnImage(kTexPlankCapR, name + "CapR", order + 1);
    return plank;
}

void GameComponents::ResultGaugeUIComponent::Awake()
{
    BuildParts();
}

void GameComponents::ResultGaugeUIComponent::BuildParts()
{
    auto* owner = GetOwner();
    if (!owner) {
        SetEnabled(false);
        return;
    }

    // オーナーは入れ物。中身は全部 Spawn で作る（ポーズメニュー・目標看板と同じ形）
    if (auto* root = dynamic_cast<UIImage*>(owner)) {
        root->SetSize({ 1.0f, 1.0f });
        root->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
    }

    // ── 今回の結果と、比べる相手を先に確定させる
    // 記録は表示の可否にかかわらず必ず残す。表示を切っただけで
    // 次回の「まえのきろく」が消えるのは、CVar の意味として分かりにくい
    runMeters_ = GameComponents::GameResultData::GetHorizontalProgressMeters();
    const auto record = GameComponents::GameRecordStore::CommitRun(runMeters_);
    previousMeters_ = record.previous;
    // CommitRun が返す best は今回を含まない。画面に出すのは今回を含めた自己最高
    bestMeters_ = (std::max)(record.best, runMeters_);
    hasPrevious_ = record.hasPrevious;
    isNewBest_ = record.isNewBest && record.hasPrevious;

    const int order = SortOrder.Get();
    BuildGauge(order);
    BuildHeadline(order + 20);
    BuildSideBoards(order + 20);
    BuildChoices(order + 30);
    BuildFooter(order + 40);

    // 舞う葉はいちばん手前
    for (Leaf& leaf : leaves_) {
        leaf.image = SpawnImage(
            Rand(0.0f, 1.0f) < 0.5f ? kTexLeafM : kTexLeafS, "ResultLeaf", order + 50);
        if (leaf.image) {
            leaf.image->SetSize({ kLeafWidth, kLeafHeight });
            leaf.image->SetActive(false);
        }
    }

    // 上の 2 隅だけ茂みを置く。下の 2 隅は Tips の帯と喧嘩するので置かない
    for (std::size_t i = 0; i < corners_.size(); ++i) {
        corners_[i] = SpawnImage(kTexFoliage, "ResultCorner" + std::to_string(i), order + 10);
        if (!corners_[i]) {
            continue;
        }
        constexpr float kScale = 1.15f;
        corners_[i]->SetSize({ kFoliageWidth * kScale, kFoliageHeight * kScale });
        corners_[i]->SetAnchoredPosition({ (i == 0) ? -944.0f : 944.0f, 4.0f });
    }

    built_ = true;
    shownMeters_ = 0.0f;
    ApplyLayout(0.0f);
}

void GameComponents::ResultGaugeUIComponent::BuildHeadline(int order)
{
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    MsdfFont* font = nullptr;
    if (fontManager) {
        MsdfFontDesc desc;
        desc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
        desc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
        desc.charsetUtf8 = kPixelCharset;
        font = fontManager->Acquire(desc);
    }
    if (!font) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "ResultGaugeUIComponent: ドットフォントを取れませんでした");
    }

    for (std::size_t row = 0; row < 2; ++row) {
        headline_[row] = SpawnPlank("ResultHeadRow" + std::to_string(row), order);
    }
    for (std::size_t i = 0; i < headCreepers_.size(); ++i) {
        headCreepers_[i] = SpawnImage(kTexVineH, "ResultHeadCreeper" + std::to_string(i), order + 4);
        if (headCreepers_[i]) {
            headCreepers_[i]->SetSize({ kCreeperWidth, kCreeperHeight });
        }
    }
    for (std::size_t i = 0; i < headFoliage_.size(); ++i) {
        headFoliage_[i] = SpawnImage(kTexFoliage, "ResultHeadFoliage" + std::to_string(i), order + 5);
        if (headFoliage_[i]) {
            headFoliage_[i]->SetSize({ kFoliageWidth * 0.85f, kFoliageHeight * 0.85f });
        }
    }
    for (std::size_t i = 0; i < headVines_.size(); ++i) {
        headVines_[i] = SpawnImage(kTexVineV, "ResultHeadVine" + std::to_string(i), order - 1);
        if (headVines_[i]) {
            headVines_[i]->SetSize({ kVineWidth, kHeadTop + 8.0f });
            headVines_[i]->SetAnchoredPosition({ kHeadVineX[i], (kHeadTop + 8.0f) * 0.5f });
        }
    }

    distanceText_ = SpawnText(font, "0ｍ", "ResultDistance", kDistanceFontSize, order + 6);
    remainText_ = SpawnText(font, "", "ResultRemain", kRemainFontSize, order + 6);

    // 右肩の「しんきろく！」。更新した回だけ出す
    newRecordPlank_ = SpawnPlank("ResultNewRecord", order + 2);
    newRecordText_ = SpawnText(font, "しんきろく！", "ResultNewRecordText",
                               kNewRecordFontSize, order + 6);
}

void GameComponents::ResultGaugeUIComponent::BuildSideBoards(int order)
{
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    MsdfFontDesc desc;
    desc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    desc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
    desc.charsetUtf8 = kPixelCharset;
    MsdfFont* font = fontManager ? fontManager->Acquire(desc) : nullptr;

    monkeyPlank_ = SpawnPlank("ResultMonkeyBoard", order);
    for (std::size_t i = 0; i < monkeyCreepers_.size(); ++i) {
        monkeyCreepers_[i] = SpawnImage(
            kTexVineH, "ResultMonkeyCreeper" + std::to_string(i), order + 4);
        if (monkeyCreepers_[i]) {
            monkeyCreepers_[i]->SetSize({ kCreeperWidth * 0.7f, kCreeperHeight * 0.7f });
        }
    }
    for (std::size_t i = 0; i < monkeyVines_.size(); ++i) {
        monkeyVines_[i] = SpawnImage(kTexVineV, "ResultMonkeyVine" + std::to_string(i), order - 1);
        if (monkeyVines_[i]) {
            const float length = kSideY - kPlankHeight * 0.5f + 8.0f;
            monkeyVines_[i]->SetSize({ kVineWidth, length });
            monkeyVines_[i]->SetAnchoredPosition({
                kSideCenterX + (i == 0 ? -kSideWidth * 0.28f : kSideWidth * 0.28f),
                length * 0.5f });
        }
    }

    const std::size_t monkeys = GameComponents::GameResultData::GetMonkeyCount();
    monkeyText_ = SpawnText(font, "つれてきたサル " + std::to_string(monkeys) + "ひき",
                            "ResultMonkeyCount", kSideFontSize, order + 6);

    rankPlank_ = SpawnPlank("ResultRankBoard", order);
    const float ratio = runMeters_ / (std::max)(1.0f, GoalDistance());
    rankText_ = SpawnText(font, std::string("しょうごう  ") + RankName(ratio),
                          "ResultRank", kRankFontSize, order + 6);

    // 自己最高は「比べる相手」の中で唯一いつでも意味を持つ値なので、
    // 記録が無い回（＝今回が最高）でも隠さずに出す。置き場所は右上（左上の板と左右対称）
    bestPlank_ = SpawnPlank("ResultBestBoard", order);
    bestText_ = SpawnText(font, "さいこうきろく " + std::to_string(bestMeters_) + "ｍ",
                          "ResultBest", kBestFontSize, order + 6);
}

void GameComponents::ResultGaugeUIComponent::BuildGauge(int order)
{
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    MsdfFontDesc desc;
    desc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    desc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
    desc.charsetUtf8 = kPixelCharset;
    MsdfFont* font = fontManager ? fontManager->Acquire(desc) : nullptr;

    // 枕木は 100m の目盛りを等分した位置に置く。こうすると 5 本ごとに目盛りの杭と重なり、
    // 「何本進んだか」と「何 m 進んだか」が画面の上で一致する
    const float pitch = TiePitch();
    const int tieCount = static_cast<int>((kGoalX - kRailLeft) / pitch + 0.001f);

    // まだ敷いていない区間の枕木（薄い板）
    for (int i = 1; i <= tieCount; ++i) {
        auto* sleeper = SpawnImage(kTexPlankMid, "ResultSleeper", order);
        if (!sleeper) {
            continue;
        }
        sleeper->SetSize({ kSleeperWidth, kSleeperHeight });
        sleeper->SetAnchoredPosition({ kRailLeft + pitch * static_cast<float>(i), kRailY });
        sleepers_.push_back(sleeper);
    }

    // 走った区間に敷くレール。専用の版下だと質感だけ写真っぽくて他の板から浮くので、
    // 板の中央パーツ（kTexPlankMid）を 1 枚だけ横に伸ばして使う。
    // 無地なので継ぎ目やパターンずれの心配が無く、伸縮も ApplyGauge で毎フレーム描き直すだけでいい
    railBar_ = SpawnImage(kTexPlankMid, "ResultRailBar", order + 1);
    if (railBar_) {
        railBar_->SetPivot({ 0.0f, 0.5f }); // 左端（0m）を基準に右へ伸ばす
        railBar_->SetSize({ 0.0f, kRailHeight });
        railBar_->SetAnchoredPosition({ kRailLeft, kRailY });
    }

    // 敷いた線をなぞるツタ
    for (float x = kRailLeft; x < kGoalX; x += kRailVineStep) {
        auto* vine = SpawnImage(kTexVineH, "ResultRailVine", order + 2);
        if (!vine) {
            continue;
        }
        vine->SetSize({ kCreeperWidth * 0.6f, kCreeperHeight * 0.6f });
        vine->SetAnchoredPosition({ x + 30.0f, kRailY - kRailHeight * 0.5f - 14.0f });
        vine->SetActive(false);
        railVines_.push_back(vine);
    }


    // 目盛り（100m ごと。最後の 1 本は目標地点そのもの）
    const int tickCount = std::clamp(
        static_cast<int>(GoalDistance() / kTickStepMeters + 0.5f), 1, kMaxTicks);
    for (int i = 0; i < tickCount; ++i) {
        auto* post = SpawnImage(kTexPlankMid, "ResultTick" + std::to_string(i), order + 2);
        if (post) {
            post->SetSize({ kTickPostWidth, kTickPostHeight });
        }
        tickPosts_.push_back(post);
        tickTexts_.push_back(SpawnText(font, "0", "ResultTickLabel" + std::to_string(i),
                                       kTickFontSize, order + 6));
    }

    // 前回の記録の杭。レール（order + 1）より後ろへ回して、ゲージの上に乗らないようにする
    recordPost_ = SpawnImage(kTexPlankCapR, "ResultRecordPost", order - 1);
    if (recordPost_) {
        recordPost_->SetSize({ kRecordPostWidth, kRecordPostBottom - kRecordPostTop });
    }
    recordPlank_ = SpawnPlank("ResultRecordBoard", order + 3);
    recordText_ = SpawnText(font, "", "ResultRecordLabel", kRecordFontSize, order + 6);

    // 目標の看板と、それを支える柱（柱は記録の杭と同じくレールより後ろ）
    gatePost_ = SpawnImage(kTexPlankCapR, "ResultGatePost", order - 1);
    if (gatePost_) {
        gatePost_->SetSize({ kGatePostWidth, kRecordPostBottom - kGatePostTop });
        gatePost_->SetAnchoredPosition({
            kGoalX, (kGatePostTop + kRecordPostBottom) * 0.5f });
    }
    gateBeam_ = SpawnPlank("ResultGateBeam", order + 4);
    for (std::size_t i = 0; i < gateFoliage_.size(); ++i) {
        gateFoliage_[i] = SpawnImage(kTexFoliage, "ResultGateFoliage" + std::to_string(i), order + 5);
        if (gateFoliage_[i]) {
            gateFoliage_[i]->SetSize({ kFoliageWidth * 0.6f, kFoliageHeight * 0.6f });
        }
    }
    gateText_ = SpawnText(font, "もくひょう 0ｍ", "ResultGateLabel", kGateFontSize, order + 6);

    cart_ = SpawnImage(kTexCart, "ResultCart", order + 7);
    if (cart_) {
        cart_->SetSize({ kCartWidth, kCartHeight });
    }
}

void GameComponents::ResultGaugeUIComponent::BuildChoices(int order)
{
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    MsdfFontDesc desc;
    desc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    desc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
    desc.charsetUtf8 = kPixelCharset;
    MsdfFont* font = fontManager ? fontManager->Acquire(desc) : nullptr;

    const auto build = [this, font, order](
        Choice& choice, const char* label, const char* name,
        const char* tweenId, float centerX) {
            choice.plank = SpawnPlank(std::string(name) + "Plank", order);
            for (std::size_t i = 0; i < choice.creepers.size(); ++i) {
                choice.creepers[i] = SpawnImage(
                    kTexVineH, std::string(name) + "Creeper" + std::to_string(i), order + 4);
                if (choice.creepers[i]) {
                    choice.creepers[i]->SetSize({ kCreeperWidth * 0.8f, kCreeperHeight * 0.8f });
                }
            }
            choice.baseFontSize = kChoiceFontSize;
            choice.baseY = kChoiceY;
            choice.text = SpawnText(font, label, name, kChoiceFontSize, order + 6);
            if (choice.text) {
                choice.text->SetAnchoredPosition({ centerX, kChoiceY });
                choice.text->SetColor(LabelColor.Get());
                // 選択・決定の演出はシーンが動かす。板はこの文字に追従させる
                choice.text->AddComponent<GameComponents::ResultButtonAnimationComponent>(tweenId);
            }
        };

    build(retry_, "もういちど", "ResultRetryButton", "result_retry_button", -kChoiceOffsetX);
    build(title_, "タイトルへ", "ResultTitleButton", "result_title_button", kChoiceOffsetX);

    cursor_ = SpawnImage(kTexCursor, "ResultCursor", order + 7);
    if (cursor_) {
        cursor_->SetSize({ kCursorWidth * 0.9f, kCursorHeight * 0.9f });
        cursor_->SetUIRotation(0.10f);
    }
    cursorX_ = -kChoiceOffsetX;
}

void GameComponents::ResultGaugeUIComponent::BuildFooter(int order)
{
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;

    // Tips の可読性を上げる半透明の帯。文字より 1 つ手前に出す必要は無いので order のまま
    // （文字・葉より後ろへ回したいだけなので、テキストより先に作って sort order を下げておく）
    tipBg_ = SpawnImage(kTexDim, "ResultTipBg", order - 1);
    if (tipBg_) {
        tipBg_->SetAnchoredPosition({ 0.0f, kTipY });
        tipBg_->SetActive(false);
    }

    // Tips は任意の漢字が来るので既定フォントに任せる（ドットフォントには漢字が無い）
    MsdfFont* tipFont = fontManager
        ? fontManager->AcquireNamed("x8y12pxDenkiChip.ttf")
        : nullptr;
    tipText_ = SpawnText(tipFont, "", "ResultTip", kTipFontSize, order);
    if (tipText_) {
        tipText_->SetAnchoredPosition({ 0.0f, kTipY });
        tipText_->SetAlign(TextAlignH::Center, TextAlignV::Middle);
        tipText_->SetActive(false);
    }
    tipLeaf_ = SpawnImage(kTexLeafM, "ResultTipLeaf", order);
    if (tipLeaf_) {
        tipLeaf_->SetSize({ kLeafWidth, kLeafHeight });
        tipLeaf_->SetActive(false);
    }
}

// ───────────────────────────────────────────────────────────────────
// 値
// ───────────────────────────────────────────────────────────────────

float GameComponents::ResultGaugeUIComponent::GoalDistance() const
{
    return (std::max)(1.0f, GoalMeters.Get());
}

float GameComponents::ResultGaugeUIComponent::TiePitch() const
{
    // 目盛り 1 つ（100m）ぶんの px を等分する。詰まりすぎるときだけ等分数を落として、
    // 「枕木が目盛りに乗る」関係だけは崩さない
    const float tickSpan = (kGoalX - kRailLeft) * (kTickStepMeters / GoalDistance());
    int ties = kTiesPerTick;
    while (ties > 1 && tickSpan / static_cast<float>(ties) < kMinTiePitch) {
        --ties;
    }
    return tickSpan / static_cast<float>(ties);
}

float GameComponents::ResultGaugeUIComponent::DistanceToX(float meters) const
{
    const float goal = GoalDistance();
    const float span = kGoalX - kRailLeft;
    if (meters <= goal) {
        return kRailLeft + (meters / goal) * span;
    }
    // 目標を越えたぶんは、ゲートの先に少しだけはみ出させる。
    // 目盛りを伸ばすと目標の重みが下がるので、スケールは goal で止める
    return kGoalX + (std::min)(kOverGoalMaxX, (meters - goal) * 0.6f);
}

Vector4 GameComponents::ResultGaugeUIComponent::Tinted(const Vector4& color, float scale) const
{
    const float gain = exposureScale_ * scale;
    return { color.x * gain, color.y * gain, color.z * gain, color.w };
}

// ───────────────────────────────────────────────────────────────────
// 更新
// ───────────────────────────────────────────────────────────────────

void GameComponents::ResultGaugeUIComponent::Update()
{
    if (!built_) {
        return;
    }
    const float deltaTime = std::clamp(Time::UnscaledDeltaTime(), 0.0f, kMaxStepSeconds);
    swayTimer_ += deltaTime;

    // UI もトーンマップ前のバッファへ描かれるので、掛かる自動露出をここで打ち消す。
    // でないと明るい場所と暗い場所で板の色が変わってしまう
    auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
    if (auto* postEffects = engine ? engine->GetService<PostEffectManager>() : nullptr) {
        if (auto* toneMapping =
            postEffects->GetEffect<ToneMapping>(PostEffectNames::ToneMapping)) {
            exposureScale_ = std::clamp(
                std::exp2(-toneMapping->GetAutoExposureEV()),
                kExposureScaleMin, kExposureScaleMax);
        }
    }

    // 選択肢の文字色だけは、露出補正を入れた値を最初の 1 フレームで書き込んでおく。
    // `ResultButtonAnimationComponent::Start()` がここで入れた色を「非選択時の色」として
    // 覚えるので、それより先に済ませる必要がある。
    // （オブジェクトは生成順に回り、入れ物であるこのオーナーは文字より先に作られている。
    //   Start は各オブジェクトの Update 直前に呼ばれるため、この順で間に合う）
    if (!choiceColorApplied_) {
        choiceColorApplied_ = true;
        for (Choice* choice : { &retry_, &title_ }) {
            if (choice->text) {
                choice->text->SetColor(Tinted(LabelColor.Get()));
            }
        }
    }

    // ── 数えあげ
    const float total = (std::max)(0.0f, CountSeconds.Get());
    const float target = static_cast<float>(runMeters_);
    if (countTimer_ < total) {
        countTimer_ += deltaTime;
        const float t = std::clamp(total > 0.0f ? countTimer_ / total : 1.0f, 0.0f, 1.0f);
        shownMeters_ = target * EasingUtil::Apply(t, EasingUtil::Type::EaseOutCubic);
    } else {
        shownMeters_ = target;
    }

    // 目盛りを通過するたびに、レールが伸びる音を上へずらしながら鳴らす
    const float tickStep = GoalDistance() * 0.2f;
    const int tickIndex = tickStep > 0.0f
        ? static_cast<int>(shownMeters_ / tickStep) : 0;
    if (tickIndex > lastTickIndex_) {
        if (lastTickIndex_ >= 0) {
            if (auto* audio = engine ? engine->GetService<AudioSystem>() : nullptr) {
                audio->PlayOneShot(
                    kSeTick,
                    { .bus = AudioBus::SE,
                      .volume = kSeTickVolume,
                      .pitch = 1.0f + 0.05f * static_cast<float>(tickIndex) });
            }
        }
        lastTickIndex_ = tickIndex;
    }

    // 前回の杭を追い越した瞬間
    if (hasPrevious_ && !passedPrevious_ && previousMeters_ > 0
        && shownMeters_ >= static_cast<float>(previousMeters_)) {
        passedPrevious_ = true;
        BurstLeaves({ DistanceToX(static_cast<float>(previousMeters_)), kRailY - 60.0f }, 5, 260.0f);
        if (auto* audio = engine ? engine->GetService<AudioSystem>() : nullptr) {
            audio->PlayOneShot(
                kSePassRecord, { .bus = AudioBus::SE, .volume = 0.5f, .pitch = 1.4f });
        }
    }

    // 到着
    if (!arrived_ && shownMeters_ >= target - 0.001f) {
        arrived_ = true;
        const bool reached = static_cast<float>(runMeters_) >= GoalDistance();
        BurstLeaves({ DistanceToX(target), kCartY }, reached ? 14 : 7, reached ? 460.0f : 300.0f);
        if (reached) {
            if (auto* audio = engine ? engine->GetService<AudioSystem>() : nullptr) {
                audio->PlayOneShot(
                    kSeArrive, { .bus = AudioBus::SE, .volume = kSeArriveVolume, .pitch = 1.0f });
            }
        }
    }

    ApplyLayout(deltaTime);
    UpdateLeaves(deltaTime);
}

void GameComponents::ResultGaugeUIComponent::PlacePlank(
    const Plank& plank, const Vector2& center, float width, float height, float angle) const
{
    const float capOffset = (width - kCapWidth) * 0.5f;
    const float midWidth = (std::max)(0.0f, width - kCapWidth * 2.0f);

    struct Part { UIImage* image; float offsetX; float sizeX; };
    const Part parts[] = {
        { plank.mid,      0.0f,       midWidth },
        { plank.capLeft,  -capOffset, kCapWidth },
        { plank.capRight, capOffset,  kCapWidth },
    };
    for (const auto& part : parts) {
        if (!part.image) {
            continue;
        }
        const Vector2 offset = RotateOffset({ part.offsetX, 0.0f }, angle);
        part.image->SetAnchoredPosition({ center.x + offset.x, center.y + offset.y });
        part.image->SetSize({ part.sizeX, height });
        part.image->SetUIRotation(angle);
    }
}

void GameComponents::ResultGaugeUIComponent::SetPlankColor(
    const Plank& plank, const Vector4& color) const
{
    for (UIImage* image : { plank.mid, plank.capLeft, plank.capRight }) {
        if (image) {
            image->SetColor(color);
        }
    }
}

void GameComponents::ResultGaugeUIComponent::ApplyLayout(float deltaTime)
{
    const float brightness = Brightness.Get();
    const Vector4 wood = Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness);
    const Vector4 woodDim = Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 0.55f);
    const float sway = std::sin(swayTimer_ * kSwaySpeed) * kSwayAngle;

    // ── 見出し（空だけを覆う 2 段）
    for (std::size_t row = 0; row < 2; ++row) {
        const float y = (row == 0) ? kHeadRow1Y : kHeadRow2Y;
        PlacePlank(headline_[row], { 0.0f, y }, kHeadWidth, kPlankHeight, sway * 0.4f);
        SetPlankColor(headline_[row], wood);
    }
    for (std::size_t i = 0; i < headCreepers_.size(); ++i) {
        if (!headCreepers_[i]) {
            continue;
        }
        const bool bottom = i >= headCreepers_.size() / 2;
        const std::size_t slot = bottom ? i - headCreepers_.size() / 2 : i;
        const float x = -kHeadWidth * 0.5f + 30.0f
            + static_cast<float>(slot) * (kHeadWidth - 60.0f) / 5.0f + (bottom ? 26.0f : 0.0f);
        headCreepers_[i]->SetAnchoredPosition({
            x, bottom ? kHeadTop + kPlankHeight * 2.0f - 4.0f : kHeadTop + 4.0f });
        headCreepers_[i]->SetUIRotation(bottom ? 3.14159265f : 0.0f);
        headCreepers_[i]->SetColor(wood);
    }
    for (std::size_t i = 0; i < headFoliage_.size(); ++i) {
        if (!headFoliage_[i]) {
            continue;
        }
        headFoliage_[i]->SetAnchoredPosition({
            (i == 0 ? -1.0f : 1.0f) * (kHeadWidth * 0.5f - 22.0f), kHeadTop + 10.0f });
        headFoliage_[i]->SetUIRotation(sway * 1.5f);
        headFoliage_[i]->SetColor(wood);
    }
    for (UIImage* vine : headVines_) {
        if (vine) {
            vine->SetColor(wood);
        }
    }

    const int meters = static_cast<int>(shownMeters_ + 0.5f);
    if (distanceText_ && meters != displayedMeters_) {
        displayedMeters_ = meters;
        distanceText_->SetText(std::to_string(meters) + "ｍ");
    }
    if (distanceText_) {
        distanceText_->SetAnchoredPosition({ 0.0f, kHeadRow1Y });
        distanceText_->SetColor(Tinted(NumberColor.Get()));
    }
    if (remainText_) {
        const float goal = GoalDistance();
        const bool reached = shownMeters_ >= goal;
        const int diff = static_cast<int>(std::abs(shownMeters_ - goal) + 0.5f);
        remainText_->SetText(reached
            ? (std::to_string(static_cast<int>(goal)) + "ｍ とっぱ！  ＋" + std::to_string(diff) + "ｍ")
            : ("もくひょうまで あと " + std::to_string(diff) + "ｍ"));
        remainText_->SetAnchoredPosition({ 0.0f, kHeadRow2Y });
        remainText_->SetColor(Tinted(reached ? AccentColor.Get() : LabelColor.Get()));
    }

    // 「しんきろく！」は更新した回だけ、到着してから出す
    const bool showNewRecord = isNewBest_ && arrived_;
    for (UIImage* image : { newRecordPlank_.mid, newRecordPlank_.capLeft, newRecordPlank_.capRight }) {
        SetActiveIf(image, showNewRecord);
    }
    SetActiveIf(newRecordText_, showNewRecord);
    if (showNewRecord) {
        const float angle = kNewRecordRotation + sway * 2.0f;
        PlacePlank(newRecordPlank_, { kNewRecordX, kNewRecordY }, kNewRecordWidth,
                   kPlankHeight, angle);
        SetPlankColor(newRecordPlank_, Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 1.3f));
        if (newRecordText_) {
            newRecordText_->SetAnchoredPosition({ kNewRecordX, kNewRecordY });
            newRecordText_->SetUIRotation(angle);
            newRecordText_->SetColor(Tinted(AccentColor.Get()));
        }
    }

    // ── 左上の板
    PlacePlank(monkeyPlank_, { kSideCenterX, kSideY }, kSideWidth, kPlankHeight);
    SetPlankColor(monkeyPlank_, wood);
    for (std::size_t i = 0; i < monkeyCreepers_.size(); ++i) {
        if (monkeyCreepers_[i]) {
            monkeyCreepers_[i]->SetAnchoredPosition({
                kSideCenterX + (i == 0 ? -1.0f : 1.0f) * kSideWidth * 0.24f,
                kSideY - kPlankHeight * 0.5f + 4.0f });
            monkeyCreepers_[i]->SetColor(wood);
        }
    }
    for (UIImage* vine : monkeyVines_) {
        if (vine) {
            vine->SetColor(wood);
        }
    }
    if (monkeyText_) {
        monkeyText_->SetAnchoredPosition({ kSideCenterX, kSideY });
        monkeyText_->SetColor(Tinted(LabelColor.Get()));
    }
    PlacePlank(rankPlank_, { kSideCenterX, kRankY }, kSideWidth, kRankHeight);
    SetPlankColor(rankPlank_, Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 1.15f));
    if (rankText_) {
        rankText_->SetAnchoredPosition({ kSideCenterX, kRankY });
        rankText_->SetColor(Tinted(AccentColor.Get()));
    }
    // ── 右上の板（自己最高）
    PlacePlank(bestPlank_, { kBestCenterX, kBestY }, kBestWidth, kBestHeight);
    SetPlankColor(bestPlank_, wood);
    if (bestText_) {
        bestText_->SetAnchoredPosition({ kBestCenterX, kBestY });
        bestText_->SetColor(Tinted(LabelColor.Get()));
    }

    ApplyGauge();

    // ── 選択肢と足元
    ApplyChoice(retry_, -kChoiceOffsetX);
    ApplyChoice(title_, kChoiceOffsetX);

    // 葉カーソルは「大きくなっているほうの札」へ寄る。
    // 選択状態そのものはシーンが持っているので、文字の大きさから読み取る
    const float selectedScale = (std::max)(
        1.001f, GameComponents::ResultButtonAnimationComponent::SelectedScale.Get());
    const auto selectionOf = [selectedScale](const Choice& choice) {
        if (!choice.text) {
            return 0.0f;
        }
        const float ratio = choice.text->GetFontSize() / (std::max)(1.0f, choice.baseFontSize);
        return std::clamp((ratio - 1.0f) / (selectedScale - 1.0f), 0.0f, 1.0f);
        };
    const bool titleFocused = selectionOf(title_) > selectionOf(retry_);
    const Choice& focused = titleFocused ? title_ : retry_;
    if (cursor_) {
        // x は札の定位置から決める（文字の位置に頼ると、演出で動いたぶんまで拾ってしまう）
        const float scale = focused.text
            ? focused.text->GetFontSize() / (std::max)(1.0f, focused.baseFontSize) : 1.0f;
        const float centerX = titleFocused ? kChoiceOffsetX : -kChoiceOffsetX;
        const float targetX = centerX - kChoiceWidth * 0.5f * scale - kCursorGap;
        cursorX_ = deltaTime > 0.0f
            ? Follow(cursorX_, targetX, kCursorFollow, deltaTime) : targetX;
        cursor_->SetAnchoredPosition({
            cursorX_, (focused.text ? focused.text->GetAnchoredPosition().y : kChoiceY) + 4.0f });
        const float alpha = focused.text ? focused.text->GetColor().w : 1.0f;
        cursor_->SetColor(Tinted({ 1.0f, 1.0f, 1.0f, alpha }, brightness * 1.2f));

    }

    if (tipText_) {
        const float pulsePhase = swayTimer_ * (6.2831853f / kTipPulseSeconds);
        const float pulse = kTipPulseMin
            + (1.0f - kTipPulseMin) * (0.5f + 0.5f * std::sin(pulsePhase));
        tipText_->SetColor(Tinted({ 0.62f, 0.68f, 0.55f, 0.95f }, pulse));
        const bool visible = tipText_->IsActive();
        SetActiveIf(tipLeaf_, visible);
        if (visible && tipLeaf_) {
            tipLeaf_->SetAnchoredPosition({
                -tipText_->GetMeasuredSize().x * 0.5f - 34.0f, kTipY + 2.0f });
            tipLeaf_->SetColor(wood);
        }
        // 帯は画面の左右端まで通す（文字幅に合わせると Tips ごとに幅が変わって落ち着かない）
        SetActiveIf(tipBg_, visible);
        if (visible && tipBg_) {
            tipBg_->SetSize({ kCanvasWidth, kTipBgHeight });
            tipBg_->SetColor({ 0.0f, 0.0f, 0.0f, kTipBgAlpha });
        }
    }
    for (UIImage* corner : corners_) {
        if (corner) {
            corner->SetColor(woodDim);
        }
    }
}

void GameComponents::ResultGaugeUIComponent::ApplyGauge()
{
    const float brightness = Brightness.Get();
    const float railBrightness = RailBrightness.Get();
    const Vector4 wood = Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness);
    const Vector4 woodDim = Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 0.55f);
    // トロッコの車体は無彩色なので、色味を足さず倍率だけ掛ける
    const Vector4 cartColor = Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, railBrightness);
    const float goal = GoalDistance();
    const float endX = DistanceToX(shownMeters_);
    const bool reached = shownMeters_ >= goal;

    // 敷いたレール（板 1 枚を左端から現在地まで伸ばすだけ。中身が無地なので伸縮に継ぎ目が出ない）
    if (railBar_) {
        const float length = (std::max)(0.0f, endX - kRailLeft);
        railBar_->SetSize({ length, kRailHeight });
        railBar_->SetColor(wood);
    }
    for (UIImage* sleeper : sleepers_) {
        if (!sleeper) {
            continue;
        }
        sleeper->SetActive(sleeper->GetAnchoredPosition().x > endX - 8.0f);
        sleeper->SetColor(Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 0.72f));
    }
    for (UIImage* vine : railVines_) {
        if (!vine) {
            continue;
        }
        vine->SetActive(vine->GetAnchoredPosition().x < endX - 24.0f);
        vine->SetColor(wood);
    }
    // 目盛り（100m ごと。最後の 1 本は目標地点なので、越えたら数字も色を変える）
    for (std::size_t i = 0; i < tickPosts_.size(); ++i) {
        const bool isGoal = (i + 1 == tickPosts_.size());
        const float meters = isGoal ? goal : kTickStepMeters * static_cast<float>(i + 1);
        const bool visible = meters <= goal + 0.5f;
        const float x = DistanceToX(meters);
        const bool passed = shownMeters_ >= meters;
        if (tickPosts_[i]) {
            SetActiveIf(tickPosts_[i], visible);
            tickPosts_[i]->SetAnchoredPosition({ x, kTickPostY });
            tickPosts_[i]->SetColor(passed ? wood : woodDim);
        }
        if (tickTexts_[i]) {
            SetActiveIf(tickTexts_[i], visible);
            tickTexts_[i]->SetText(std::to_string(static_cast<int>(meters + 0.5f)));
            tickTexts_[i]->SetAnchoredPosition({ x, kTickLabelY });
            tickTexts_[i]->SetColor(Tinted(
                (isGoal && reached) ? AccentColor.Get()
                : passed ? LabelColor.Get()
                : Vector4{ 0.16f, 0.20f, 0.15f, 1.0f }));
        }
    }

    // 前回の記録の杭
    const bool showRecord = hasPrevious_ && previousMeters_ > 0 && ShowPreviousRecord.Get();
    SetActiveIf(recordPost_, showRecord);
    for (UIImage* image : { recordPlank_.mid, recordPlank_.capLeft, recordPlank_.capRight }) {
        SetActiveIf(image, showRecord);
    }
    SetActiveIf(recordText_, showRecord);
    if (showRecord) {
        const float x = DistanceToX(static_cast<float>(previousMeters_));
        if (recordPost_) {
            recordPost_->SetAnchoredPosition({
                x, (kRecordPostTop + kRecordPostBottom) * 0.5f });
            recordPost_->SetColor(wood);
        }
        // 札は杭の真上に置いて、どの位置の記録なのかを迷わせない。
        // トロッコを避けて上げたぶん目標の看板と同じ高さになったので、
        // 看板に触れる手前で左へ逃がす（看板の左端は kGoalX - kGateBeamWidth * 0.5）
        const float labelX = (std::min)(
            x, kGoalX - kGateBeamWidth * 0.5f - kRecordPlankWidth * 0.5f - 10.0f);
        PlacePlank(recordPlank_, { labelX, kRecordPlankY }, kRecordPlankWidth, kRecordPlankHeight);
        SetPlankColor(recordPlank_, wood);
        if (recordText_) {
            recordText_->SetText("まえのきろく " + std::to_string(previousMeters_) + "ｍ");
            recordText_->SetAnchoredPosition({ labelX, kRecordPlankY });
            recordText_->SetColor(Tinted({ 0.62f, 0.68f, 0.55f, 1.0f }));
        }
    }

    // 目標の看板。越えると板が傾いて明るくなる
    const Vector4 gateWood = reached
        ? Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 1.2f)
        : Tinted({ 1.0f, 1.0f, 1.0f, 1.0f }, brightness * 0.9f);
    if (gatePost_) {
        gatePost_->SetColor(gateWood);
    }
    const float beamAngle = reached ? -0.055f : 0.0f;
    PlacePlank(gateBeam_, { kGoalX, kGateBeamY }, kGateBeamWidth, kPlankHeight, beamAngle);
    SetPlankColor(gateBeam_, gateWood);
    for (std::size_t i = 0; i < gateFoliage_.size(); ++i) {
        if (gateFoliage_[i]) {
            gateFoliage_[i]->SetAnchoredPosition({
                kGoalX + (i == 0 ? -178.0f : 178.0f), kGateBeamY - 28.0f });
            gateFoliage_[i]->SetColor(wood);
        }
    }
    if (gateText_) {
        gateText_->SetText("もくひょう " + std::to_string(static_cast<int>(goal + 0.5f)) + "ｍ");
        gateText_->SetAnchoredPosition({ kGoalX, kGateBeamY });
        gateText_->SetUIRotation(beamAngle);
        gateText_->SetColor(Tinted(reached ? AccentColor.Get() : NumberColor.Get()));
    }
    if (cart_) {
        cart_->SetAnchoredPosition({ endX - 4.0f, kCartY });
        cart_->SetColor(cartColor);
    }
}

void GameComponents::ResultGaugeUIComponent::ApplyChoice(Choice& choice, float centerX)
{
    if (!choice.text) {
        return;
    }
    // 板は文字に従う。文字はシーンの ResultButtonAnimationComponent が動かしているので、
    // ここで位置や大きさを書き戻してはいけない
    const Vector2 textPosition = choice.text->GetAnchoredPosition();
    const float scale = choice.text->GetFontSize() / (std::max)(1.0f, choice.baseFontSize);
    const float selectedScale = (std::max)(
        1.001f, GameComponents::ResultButtonAnimationComponent::SelectedScale.Get());
    const float selection = std::clamp((scale - 1.0f) / (selectedScale - 1.0f), 0.0f, 1.0f);
    const float alpha = choice.text->GetColor().w;
    const float angle = kChoiceTilt * selection;
    const float width = kChoiceWidth * scale;

    PlacePlank(choice.plank, { textPosition.x, textPosition.y }, width,
               kPlankHeight * scale, angle);
    const Vector4 color = Tinted(
        { 1.0f, 1.0f, 1.0f, alpha }, Brightness.Get() * (1.0f + 0.28f * selection));
    SetPlankColor(choice.plank, color);

    for (std::size_t i = 0; i < choice.creepers.size(); ++i) {
        if (!choice.creepers[i]) {
            continue;
        }
        const Vector2 offset = RotateOffset(
            { (static_cast<float>(i) - 1.0f) * width * 0.30f,
              -kPlankHeight * 0.5f * scale + 4.0f },
            angle);
        choice.creepers[i]->SetAnchoredPosition({
            textPosition.x + offset.x, textPosition.y + offset.y });
        choice.creepers[i]->SetUIRotation(angle);
        choice.creepers[i]->SetColor(color);
    }
    (void)centerX;
}

// ───────────────────────────────────────────────────────────────────
// 舞う葉
// ───────────────────────────────────────────────────────────────────

void GameComponents::ResultGaugeUIComponent::BurstLeaves(
    const Vector2& origin, int count, float power)
{
    int spawned = 0;
    for (Leaf& leaf : leaves_) {
        if (spawned >= count) {
            break;
        }
        if (!leaf.image || leaf.life > 0.0f) {
            continue;
        }
        const float angle = Rand(-2.6f, -0.5f);
        const float speed = power * Rand(0.6f, 1.25f);
        leaf.position = { origin.x + Rand(-26.0f, 26.0f), origin.y + Rand(-16.0f, 16.0f) };
        leaf.velocity = { std::cos(angle) * speed * 0.7f, std::sin(angle) * speed };
        leaf.rotation = Rand(-3.14f, 3.14f);
        leaf.spin = Rand(-6.0f, 6.0f);
        leaf.lifeSpan = Rand(0.8f, 1.5f);
        leaf.life = leaf.lifeSpan;
        leaf.size = Rand(0.8f, 1.4f);
        leaf.image->SetActive(true);
        ++spawned;
    }
}

void GameComponents::ResultGaugeUIComponent::UpdateLeaves(float deltaTime)
{
    const float brightness = Brightness.Get();
    for (Leaf& leaf : leaves_) {
        if (!leaf.image || leaf.life <= 0.0f) {
            continue;
        }
        leaf.life -= deltaTime;
        if (leaf.life <= 0.0f) {
            leaf.image->SetActive(false);
            continue;
        }
        leaf.velocity.y += kLeafGravity * deltaTime;
        leaf.velocity.x *= 0.99f;
        leaf.position.x += leaf.velocity.x * deltaTime;
        leaf.position.y += leaf.velocity.y * deltaTime;
        leaf.rotation += leaf.spin * deltaTime;

        const float alpha = std::clamp(leaf.life / kLeafFadeSeconds, 0.0f, 1.0f);
        leaf.image->SetAnchoredPosition(leaf.position);
        leaf.image->SetSize({ kLeafWidth * leaf.size, kLeafHeight * leaf.size });
        leaf.image->SetUIRotation(leaf.rotation);
        leaf.image->SetColor(Tinted({ 1.0f, 1.0f, 1.0f, alpha }, brightness));
    }
}
