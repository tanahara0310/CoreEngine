#include "pch.h"
#include "SpeedGaugeUIComponent.h"

#include "Components/Train/TrainMovementComponent.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include "EngineSystem/EngineSystem.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/CVarPanel.h"
#endif

using namespace CoreEngine;

namespace
{
    // ───────────────────────────────────────────────────────────────
    // テクスチャ。スタミナゲージと同じ版下を流用するので新規アセットは無い
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTexBoardMid = "Application/Assets/Textures/Stamina/board_mid.png";
    constexpr const char* kTexBoardCapL = "Application/Assets/Textures/Stamina/board_cap_l.png";
    constexpr const char* kTexBoardCapR = "Application/Assets/Textures/Stamina/board_cap_r.png";
    constexpr const char* kTexVine = "Application/Assets/Textures/Stamina/vine.png";

    // ───────────────────────────────────────────────────────────────
    // 版下の寸法。スタミナゲージの kArtScale と揃えること
    // ───────────────────────────────────────────────────────────────
    constexpr float kArtScale = 2.0f;

    constexpr float kPanelHeight = 38.0f * kArtScale;  ///< board_mid.png の高さと一致させる
    constexpr float kCapWidth = 14.0f * kArtScale;
    constexpr float kInnerPadding = 10.0f * kArtScale;
    /// 登場アニメーションで引っ込めるときに、板の右端を画面外へ出しておく余白 [px]
    constexpr float kIntroMargin = 48.0f;

    /// board_mid.png に焼かれている枠の太さ（上下それぞれ）。実測 12px なので中身は 52px しかない
    constexpr float kBoardFrameInset = 6.0f * kArtScale;
    /// 枠と数字のあいだに空ける余白。詰めると字が枠に触れて読みにくくなる
    constexpr float kDigitBreathing = 5.0f * kArtScale;
    constexpr float kVineWidth = 52.0f * kArtScale;
    constexpr float kVineHeight = 19.0f * kArtScale;
    constexpr std::size_t kVineCount = 6;
    constexpr float kVineOverhang = 6.0f * kArtScale;

    /// 数字 1 桁ぶんの送り幅。窓は描かないので、字が触れ合わない間隔だけ取る
    constexpr float kDigitWidth = 18.0f * kArtScale;
    constexpr float kDigitGap = 2.5f * kArtScale;
    constexpr float kDotWidth = 7.0f * kArtScale;
    constexpr float kDotSideGap = 2.0f * kArtScale;
    constexpr float kUnitGap = 6.0f * kArtScale;
    constexpr float kUnitWidth = 32.0f * kArtScale;   ///< "km/h" の見込み幅
    constexpr float kDigitFontSize = 19.0f * kArtScale;
    constexpr float kUnitFontSize = 11.0f * kArtScale;
    /// 小数点は数字より下に置くと、桁の並びが読みやすくなる
    constexpr float kDotBaselineOffset = 5.0f * kArtScale;

    /// ドット絵フォントの数字は em のおよそ 0.75 倍の高さになる
    constexpr float kDigitHeightRatio = 0.75f;
    // 数字が枠に触れていないことをここで担保する。フォントを大きくしたらここで気づける
    static_assert(
        kDigitFontSize * kDigitHeightRatio <=
        kPanelHeight - (kBoardFrameInset + kDigitBreathing) * 2.0f,
        "数字が板の枠に触れます。kDigitFontSize を下げるか kDigitBreathing を詰めてください");

    /// 整数 3 桁 ＋ 小数点 ＋ 小数 1 桁 ＋ 単位
    constexpr float kContentWidth =
        kDigitWidth * 3.0f + kDigitGap * 2.0f +
        kDotSideGap + kDotWidth + kDotSideGap +
        kDigitWidth +
        kUnitGap + kUnitWidth;

    // ───────────────────────────────────────────────────────────────
    // 桁送り
    // ───────────────────────────────────────────────────────────────
    constexpr float kRollSeconds = 0.14f;               ///< 1 桁が入れ替わる時間
    constexpr float kRollTravel = 4.5f * kArtScale;     ///< 送り出される距離

    /// 表示できる上限。3 桁＋小数 1 桁なので 999.9 km/h
    constexpr float kMaxDisplayKmh = 999.9f;

    // ───────────────────────────────────────────────────────────────
    // 針の振れ。停車中は 0 を出し、発車したら一気に上げる
    // ───────────────────────────────────────────────────────────────
    /// 上がるときの速さ [km/h 毎秒]。発車時に 0 から本来の速度まで 1 秒かからずに届く。
    /// 走行中の加速（既定で毎秒 0.72 km/h）はこれよりずっと緩いので、追いついた後は素通しになる
    constexpr float kRiseRate = 22.0f;
    /// 下がるときの速さ [km/h 毎秒]。駅の減速と停車がゆっくり落ちて見える
    constexpr float kFallRate = 38.0f;
    /// これ以下しか動いていなければ停車とみなす [ワールド単位]
    constexpr float kMovingEpsilonSquared = 1.0e-8f;

    // ───────────────────────────────────────────────────────────────
    // 色。スタミナゲージの粒（バナナ）と輪郭に合わせてある
    // ───────────────────────────────────────────────────────────────
    const Vector4 kDigitColor{ 0.980f, 0.839f, 0.200f, 1.0f };  ///< #FAD633
    const Vector4 kDotColor{ 0.573f, 0.341f, 0.024f, 1.0f };    ///< #925706
    const Vector4 kUnitColor{ 0.620f, 0.565f, 0.471f, 1.0f };   ///< #9E9078
    const Vector4 kOutlineColor{ 0.031f, 0.020f, 0.012f, 1.0f };///< #080503
    constexpr float kOutlineWidth = 0.045f;

    // ───────────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターから編集できる）
    // ───────────────────────────────────────────────────────────────
    CVar<bool> cvEnabled{
        "Game.SpeedGauge.Enabled", true,
        "トロッコの速度計を表示する" };

    CVar<Vector2> cvPosition{
        "Game.SpeedGauge.Position", { 32.0f, 152.0f },
        "画面左上を基準にした位置 [px]（基準解像度 1920x1080）。"
        "既定はスタミナゲージ（y=44・高さ 76）の真下で、蔦どうしが触れない分だけ空けてある。"
        "Game.StaminaGauge.Position を動かしたらこちらも合わせること",
        CVarRange{ -2000.0f, 2000.0f } };

    CVar<float> cvScale{
        "Game.SpeedGauge.Scale", 1.3f,
        "速度計全体の表示倍率。1.0 はテクスチャの原寸でドットが一切ボケないが、"
        "1080p では数字が 28px ほどにしかならず、走りながらでは読み取れない。"
        "数字と単位は MSDF フォントなので拡大してもボケず、甘くなるのは板・端木・蔦だけ",
        CVarRange{ 0.25f, 2.5f } };

    CVar<float> cvBoardBrightness{
        "Game.SpeedGauge.BoardBrightness", 0.68f,
        "板と端木の濃さ。1 でテクスチャそのまま、下げるほど濃い木になる",
        CVarRange{ 0.2f, 1.5f } };

    CVar<float> cvBoardGreenTint{
        "Game.SpeedGauge.BoardGreenTint", 0.22f,
        "板を緑へ寄せる強さ。上げるほど湿ったジャングルの木らしい色みになる",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvMetersPerCell{
        "Game.SpeedGauge.MetersPerCell", 2.0f,
        "レール 1 マスを何メートルとみなすか。km/h = 速度[マス/秒] x この値 x 3.6",
        CVarRange{ 0.1f, 20.0f } };

    CVar<int> cvSortOrder{
        "Game.SpeedGauge.SortOrder", 880,
        "速度計の描画順（大きいほど手前）",
        CVarRange{ 0.0f, 5000.0f } };

    CVar<float> cvSwaySpeed{
        "Game.SpeedGauge.SwaySpeed", 1.8f,
        "蔦が揺れるはやさ",
        CVarRange{ 0.0f, 10.0f } };

    CVar<float> cvFoliageBrightness{
        "Game.SpeedGauge.FoliageBrightness", 0.62f,
        "蔦の明るさ。ブルームで白く飛ぶので下げて使うが、板を濃くしたぶん少し戻してある",
        CVarRange{ 0.05f, 2.0f } };

    // ───────────────────────────────────────────────────────────────
    // 駅の減速フラッシュ。PlaySlowdownFlash() で 1 回だけ鳴らす
    // ───────────────────────────────────────────────────────────────
    CVar<float> cvSlowdownSeconds{
        "Game.SpeedGauge.SlowdownSeconds", 1.1f,
        "駅で減速したときに、速度計が赤く沈んでいる時間 [秒]。"
        "桁が落ちきるまで（落下レート 38km/h 毎秒）より長く取ってある。"
        "落ちきる前に色が戻ると、赤が何を指していたのか結び付かない",
        CVarRange{ 0.05f, 4.0f } };

    CVar<float> cvSlowdownDip{
        "Game.SpeedGauge.SlowdownDip", 14.0f,
        "減速したときに速度計全体が沈む深さ [px]（基準解像度 1920x1080）。"
        "数字だけでなく板ごと沈めるのは、視線が数字に無くても端で動きに気づけるようにするため",
        CVarRange{ 0.0f, 60.0f } };

    CVar<float> cvSlowdownBlinks{
        "Game.SpeedGauge.SlowdownBlinks", 3.0f,
        "赤く点滅する回数。1 未満にすると点滅せず、赤くなって戻るだけになる",
        CVarRange{ 0.0f, 8.0f } };

    CVar<float> cvSlowdownFullDropKmh{
        "Game.SpeedGauge.SlowdownFullDropKmh", 20.0f,
        "この km/h ぶん落ちたら演出が最大の強さになる。"
        "駅は加速ぶんを丸ごと最低速度まで落とすので、落差は走ってきた距離で変わる。"
        "小さな落差で毎回最大に振れると駅がうるさくなるため、落差に比例させている。"
        "既定は Game.StationSlowdown.FullDropCells（3 マス/秒 ≒ 21.6km/h）に揃えてある",
        CVarRange{ 0.5f, 100.0f } };

    CVar<float> cvSlowdownBoardTint{
        "Game.SpeedGauge.SlowdownBoardTint", 0.55f,
        "減速中に板と端木を赤へ寄せる強さ。0 で板は色を変えない。"
        "数字だけ赤くしても面積が小さく、視界の端では色が変わったと気づけない",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvSlowdownDropFall{
        "Game.SpeedGauge.SlowdownDropFall", 30.0f,
        "「▼18.4」が板の下へ落ちていく距離 [px]（基準解像度 1920x1080）。"
        "上へ浮かせず下へ落とすのは、動く向きそのものに「下がった」と言わせるため",
        CVarRange{ 0.0f, 200.0f } };

    CVar<float> cvSlowdownDropFontSize{
        "Game.SpeedGauge.SlowdownDropFontSize", 15.0f,
        "「▼18.4」の文字の大きさ。速度計と同じ倍率が掛かる",
        CVarRange{ 4.0f, 48.0f } };

    // ───────────────────────────────────────────────────────────────
    // 減速の赤。ここの値は「画面に出る色」ではなく露出前のリニア値。
    //
    // 昼のゲームシーンは実効ゲインが約 4.5 倍あり、0.25 を超えた成分は何色でも
    // 白へ飽和する。上の kDigitColor（0.98, 0.84, 0.20）が黄色ではなく
    // ほぼ白に見えているのがその実例で、ここで素直に「赤 = (1.0, 0.3, 0.2)」と
    // 書くと R も G も飽和して、ただの白になる（＝色が変わったと分からない）。
    //
    // 赤に見せるには R を 0.2〜0.4 に置き、G と B を 0.01 前後まで落とす。
    // 実測した対応（リニア → 画面 0-255）:
    //   0.44 → 245 ／ 0.20 → 227 ／ 0.10 → 199 ／ 0.04 → 141 ／ 0.012 → 67
    // ───────────────────────────────────────────────────────────────

    /// 減速中に数字を寄せる色。画面上でおよそ (238, 60, 45) の強い赤になる
    const Vector4 kSlowdownDigitColor{ 0.300f, 0.010f, 0.006f, 1.0f };
    /// 板と端木を寄せる色。テクスチャに掛かる倍率なので、R は base と同じ高さに残して
    /// G と B だけ潰す。明度を変えずに色だけ赤へ振るための値
    const Vector4 kSlowdownBoardColor{ 0.550f, 0.045f, 0.028f, 1.0f };
    /// 「▼18.4」の色。数字より一段明るい赤にして、板の上でいちばん先に目に入るようにする
    const Vector4 kDropLabelColor{ 0.420f, 0.014f, 0.008f, 1.0f };
    /// 板の下端と「▼18.4」のあいだに空ける余白（蔦に重ならない高さ）
    constexpr float kDropLabelGap = 11.0f * kArtScale;
    /// 出た瞬間に膨らむ量。0.5 で 1.5 倍から始まる
    constexpr float kDropLabelPunch = 0.5f;
    /// 膨らみが収まるまでの割合。短く取って、飛び出した勢いだけを残す
    constexpr float kDropLabelPunchSpan = 0.18f;
    /// この割合まではまったく薄めない。読み取る時間を先に確保する
    constexpr float kDropLabelHoldPhase = 0.6f;

    /// @brief 板と同じ設定で UIImage を 1 枚生やす
    UIImage* SpawnPart(GameObject* owner, const char* texture, const std::string& name, int sortOrder)
    {
        auto* image = owner->Spawn<UIImage>();
        if (!image) {
            return nullptr;
        }
        image->Initialize(texture, name);
        image->SetSerializeEnabled(false);
        image->SetAnchor(UIAnchor::TopLeft);
        image->SetSortOrder(sortOrder);
        return image;
    }

    /// @brief 数字・記号用の UIText を 1 枚生やす
    UIText* SpawnLabel(GameObject* owner, MsdfFont* font, const std::string& textUtf8,
        const std::string& name, float fontSize, const Vector4& color, int sortOrder)
    {
        auto* text = owner->Spawn<UIText>();
        if (!text) {
            return nullptr;
        }
        text->Initialize(font, textUtf8, name);
        text->SetSerializeEnabled(false);
        text->SetAnchor(UIAnchor::TopLeft);
        text->SetPivot({ 0.5f, 0.5f });
        text->SetFontSize(fontSize);
        text->SetColor(color);
        text->SetOutline(kOutlineColor, kOutlineWidth);
        text->SetSortOrder(sortOrder);
        return text;
    }

    /// @brief 送り出しの緩急。止まり際をなめらかにする
    float RollEase(float t)
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        return 1.0f - (1.0f - clamped) * (1.0f - clamped);
    }

    /// @brief current から target へ、1 フレームぶんの上限 maxDelta まで近づける
    float MoveTowards(float current, float target, float maxDelta)
    {
        const float diff = target - current;
        if (std::abs(diff) <= maxDelta) {
            return target;
        }
        return current + (diff > 0.0f ? maxDelta : -maxDelta);
    }
}

/// @note 上限を 2 まで許すのは、EaseOutBack を通した「行き過ぎ」をそのまま活かすため
void GameComponents::SpeedGaugeUIComponent::SetIntroReveal(float reveal)
{
    introReveal_ = std::clamp(reveal, 0.0f, 2.0f);
}

void GameComponents::SpeedGaugeUIComponent::Awake()
{
    if (!train_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "SpeedGaugeUIComponent: TrainMovement が未設定です");
        SetEnabled(false);
        return;
    }
    BuildParts();
}

void GameComponents::SpeedGaugeUIComponent::BuildParts()
{
    auto* owner = GetOwner();
    if (!owner) {
        return;
    }

    // オーナー自身が中板。横方向に一様なテクスチャなので、伸ばしてもドットが崩れない
    board_ = dynamic_cast<UIImage*>(owner);
    if (!board_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "SpeedGaugeUIComponent: UIImage にアタッチしてください");
        SetEnabled(false);
        return;
    }

    const int baseOrder = cvSortOrder.Get();
    board_->SetAnchor(UIAnchor::TopLeft);
    board_->SetPivot({ 0.0f, 0.0f });
    board_->SetSortOrder(baseOrder);

    capLeft_ = SpawnPart(owner, kTexBoardCapL, "SpeedGaugeCapL", baseOrder + 1);
    capRight_ = SpawnPart(owner, kTexBoardCapR, "SpeedGaugeCapR", baseOrder + 1);
    for (auto* cap : { capLeft_, capRight_ }) {
        if (cap) {
            cap->SetPivot({ 0.0f, 0.0f });
            cap->SetSize({ kCapWidth, kPanelHeight });
        }
    }

    // 板の縁へ絡ませる蔦。スタミナゲージと同じ本数・同じ揺らし方にしてある
    vines_.reserve(kVineCount);
    for (std::size_t i = 0; i < kVineCount; ++i) {
        auto* vine = SpawnPart(
            owner, kTexVine, "SpeedGaugeVine_" + std::to_string(i), baseOrder + 2);
        if (vine) {
            vine->SetPivot({ 0.5f, 0.5f });
            vine->SetSize({ kVineWidth, kVineHeight });
            vines_.push_back(vine);
        }
    }

    auto* engine = owner->GetEngineSystem();
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    if (!fontManager) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "SpeedGaugeUIComponent: FontManager が無いため数字を出しません");
        built_ = true;
        return;
    }

    // 数字はドット感のあるフォントで打つ（MapView の距離表示と同じもの）
    MsdfFontDesc digitFontDesc;
    digitFontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    digitFontDesc.systemFamilyNames = { L"Segoe UI" };
    digitFontDesc.charsetUtf8 = "0123456789.";
    auto* digitFont = fontManager->Acquire(digitFontDesc);

    // 単位はタイトルの決定（スタート）UIと同じフォントで揃える
    MsdfFontDesc unitFontDesc;
    unitFontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    unitFontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
    unitFontDesc.charsetUtf8 = "km/h";
    auto* unitFont = fontManager->Acquire(unitFontDesc);

    if (digitFont) {
        for (std::size_t i = 0; i < kDigitCount; ++i) {
            const std::string suffix = std::to_string(i);
            digits_[i].current = SpawnLabel(
                owner, digitFont, "0", "SpeedGaugeDigit_" + suffix,
                kDigitFontSize, kDigitColor, baseOrder + 4);
            digits_[i].outgoing = SpawnLabel(
                owner, digitFont, "0", "SpeedGaugeDigitOut_" + suffix,
                kDigitFontSize, kDigitColor, baseOrder + 3);
            if (digits_[i].outgoing) {
                digits_[i].outgoing->SetActive(false);
            }
        }
        dot_ = SpawnLabel(
            owner, digitFont, ".", "SpeedGaugeDot",
            kDigitFontSize, kDotColor, baseOrder + 4);
    }
    if (unitFont) {
        unit_ = SpawnLabel(
            owner, unitFont, "km/h", "SpeedGaugeUnit",
            kUnitFontSize, kUnitColor, baseOrder + 4);
        if (unit_) {
            unit_->SetPivot({ 0.0f, 0.5f });
        }
    }

    // 減速したときだけ出る「▼18.4」。桁が落ちる過程を追えなくても、
    // 落ちた量そのものが数字で残るようにするためのもの。
    // 数字は本体と同じドット絵フォントで、三角だけシステムフォントへ落ちる
    // （レール方向ガイドの矢印と同じ組み合わせ）。
    MsdfFontDesc dropFontDesc;
    dropFontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    dropFontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"MS Gothic" };
    dropFontDesc.charsetUtf8 = "▼0123456789.";
    if (auto* dropFont = fontManager->Acquire(dropFontDesc)) {
        dropLabel_ = SpawnLabel(
            owner, dropFont, "▼0.0", "SpeedGaugeDrop",
            cvSlowdownDropFontSize.Get(), kDropLabelColor, baseOrder + 5);
        if (dropLabel_) {
            dropLabel_->SetPivot({ 0.5f, 0.5f });
            dropLabel_->SetActive(false);
        }
    }

    built_ = true;
}

float GameComponents::SpeedGaugeUIComponent::CalculateKilometersPerHour() const
{
    if (!train_) {
        return 0.0f;
    }
    // 速度はマス/秒。1 マスの実距離を掛けて m/s にしてから km/h へ直す
    return train_->GetMoveSpeed() * std::max(0.0f, cvMetersPerCell.Get()) * 3.6f;
}

void GameComponents::SpeedGaugeUIComponent::PlaySlowdownFlash(float droppedCellsPerSecond)
{
    if (droppedCellsPerSecond <= 0.0f) {
        return;
    }
    const float droppedKmh =
        droppedCellsPerSecond * std::max(0.0f, cvMetersPerCell.Get()) * 3.6f;
    const float strength =
        std::clamp(droppedKmh / std::max(cvSlowdownFullDropKmh.Get(), 0.01f), 0.0f, 1.0f);
    // 落差が小さいほど弱くはするが、鳴らすと決めた以上は下限を残す。
    // 見えるか見えないかの演出は「出ていない」と同じで、初見への説明にならない
    slowdownStrength_ = std::max(strength, 0.35f);
    slowdownDropKmh_ = droppedKmh;
    // 連続する駅で重ねず、毎回頭から鳴らし直す（駅が続くほど強くなっても意味が無い）
    slowdownElapsed_ = 0.0f;

    if (dropLabel_) {
        // 小数 1 桁。オドメーターと同じ精度で「いくつ落ちたか」を出す
        const int scaled =
            static_cast<int>(std::clamp(droppedKmh, 0.0f, kMaxDisplayKmh) * 10.0f + 0.5f);
        dropLabel_->SetText(
            "▼" + std::to_string(scaled / 10) + "." + std::to_string(scaled % 10));
    }
}

float GameComponents::SpeedGaugeUIComponent::GetSlowdownProgress() const
{
    if (slowdownElapsed_ < 0.0f) {
        return 1.0f;
    }
    return std::clamp(slowdownElapsed_ / std::max(cvSlowdownSeconds.Get(), 0.01f), 0.0f, 1.0f);
}

bool GameComponents::SpeedGaugeUIComponent::UpdateTrainMovingState()
{
    if (!train_) {
        return false;
    }

    const Vector3 position = train_->GetWorldPosition();
    if (!hasTrainPosition_) {
        lastTrainPosition_ = position;
        hasTrainPosition_ = true;
        return false;
    }

    // 投石ジャンプで y だけ動くことがあるので、水平方向だけを見る
    const float dx = position.x - lastTrainPosition_.x;
    const float dz = position.z - lastTrainPosition_.z;
    lastTrainPosition_ = position;
    return (dx * dx + dz * dz) > kMovingEpsilonSquared;
}

void GameComponents::SpeedGaugeUIComponent::Update()
{
    if (!built_ || !board_) {
        return;
    }

    const bool enabled = cvEnabled.Get();
    if (board_->IsActive() != enabled) {
        board_->SetActive(enabled);
        for (auto* part : { capLeft_, capRight_ }) {
            if (part) {
                part->SetActive(enabled);
            }
        }
        for (auto* vine : vines_) {
            if (vine) {
                vine->SetActive(enabled);
            }
        }
        for (auto& digit : digits_) {
            if (digit.current) {
                digit.current->SetActive(enabled);
            }
            // 送り出し中に消すと数字が取り残されるので、こちらは落とすときだけ触る
            if (digit.outgoing && !enabled) {
                digit.outgoing->SetActive(false);
            }
        }
        for (auto* text : { dot_, unit_ }) {
            if (text) {
                text->SetActive(enabled);
            }
        }
        // 落差の表示は減速中しか出さない。表示を戻したときに前回のぶんが残らないよう、
        // 消すときだけ触って、出すのは LayoutDropLabel に任せる
        if (dropLabel_ && !enabled) {
            dropLabel_->SetActive(false);
        }
    }
    if (!enabled) {
        return;
    }

    const float deltaTime = Time::UnscaledDeltaTime();
    elapsed_ += deltaTime;

    // 減速フラッシュ。終わったら負へ戻して、以降は等倍・元色で描く
    if (slowdownElapsed_ >= 0.0f) {
        slowdownElapsed_ += deltaTime;
        if (slowdownElapsed_ >= cvSlowdownSeconds.Get()) {
            slowdownElapsed_ = -1.0f;
        }
    }

    // 発車前と停止中は 0。走り出したら 0 から本来の速度まで一気に振り切る
    const bool moving = UpdateTrainMovingState();
    const float target = moving ? CalculateKilometersPerHour() : 0.0f;
    const float rate = (target > displayedKilometersPerHour_) ? kRiseRate : kFallRate;
    displayedKilometersPerHour_ =
        MoveTowards(displayedKilometersPerHour_, target, rate * deltaTime);

    UpdateDigits(displayedKilometersPerHour_, deltaTime);
    LayoutParts(elapsed_);
}

void GameComponents::SpeedGaugeUIComponent::UpdateDigits(float kilometersPerHour, float deltaTime)
{
    // 小数 1 桁まで見せるので、10 倍した整数にしてから桁をばらす
    const float shown = std::clamp(kilometersPerHour, 0.0f, kMaxDisplayKmh);
    int scaled = static_cast<int>(shown * 10.0f + 0.5f);

    int wanted[kDigitCount]{};
    for (std::size_t i = 0; i < kDigitCount; ++i) {
        // 添字 0 が百の位なので、小さい桁から詰めて逆順に入れる
        wanted[kDigitCount - 1 - i] = scaled % 10;
        scaled /= 10;
    }

    for (std::size_t i = 0; i < kDigitCount; ++i) {
        Digit& digit = digits_[i];
        if (!digit.current) {
            continue;
        }

        if (digit.value != wanted[i]) {
            // 9 から 0 へ繰り上がるときも、見た目は上へ送りたい
            const bool increased = digit.value < 0 ||
                (wanted[i] > digit.value ? (wanted[i] - digit.value) <= 5
                                         : (digit.value - wanted[i]) > 5);
            digit.direction = increased ? 1.0f : -1.0f;
            digit.outgoingValue = digit.value;
            digit.value = wanted[i];
            digit.roll = (digit.outgoingValue < 0) ? 1.0f : 0.0f;
            digit.current->SetText(std::to_string(digit.value));
            if (digit.outgoing) {
                const bool showsOutgoing = digit.outgoingValue >= 0;
                digit.outgoing->SetActive(showsOutgoing);
                if (showsOutgoing) {
                    digit.outgoing->SetText(std::to_string(digit.outgoingValue));
                }
            }
        }

        if (digit.roll < 1.0f) {
            digit.roll = std::min(1.0f, digit.roll + deltaTime / kRollSeconds);
            if (digit.roll >= 1.0f && digit.outgoing) {
                digit.outgoing->SetActive(false);
            }
        }
    }
}

void GameComponents::SpeedGaugeUIComponent::LayoutParts(float time)
{
    const float scale = cvScale.Get();
    const float panelH = kPanelHeight * scale;
    const float capW = kCapWidth * scale;
    const float padding = kInnerPadding * scale;
    const float panelWidth = capW * 2.0f + padding * 2.0f + kContentWidth * scale;

    // 駅の減速フラッシュ。沈み込みは板ごと掛け、赤みは数字だけに掛ける。
    // 板ごと動かすのは、数字を読んでいなくても画面の端で「何か起きた」と気づけるようにするため
    const float slowdownPhase = GetSlowdownProgress();
    const float slowdownFade = (slowdownPhase < 1.0f) ? slowdownStrength_ : 0.0f;
    // 正弦の半周期。ストンと沈んで、同じ形で戻ってくる
    const float slowdownDip =
        std::sin(slowdownPhase * MathCore::Constants::kPi) * cvSlowdownDip.Get() * slowdownFade;
    // 前半は赤のまま保ち、後半で元の色へ戻す。頭から薄め始めると、
    // 桁が落ちきる（落下レート 38km/h 毎秒）より先に赤が消えて、何を指した色か分からなくなる
    constexpr float kRedHoldPhase = 0.55f;
    const float redDecay = (slowdownPhase <= kRedHoldPhase)
        ? 1.0f
        : 1.0f - (slowdownPhase - kRedHoldPhase) / (1.0f - kRedHoldPhase);
    const float slowdownEnvelope = redDecay * slowdownFade;
    const float blinks = std::max(cvSlowdownBlinks.Get(), 0.0f);
    const float slowdownRed = (blinks >= 1.0f)
        ? (0.5f + 0.5f *
              std::cos(slowdownPhase * 2.0f * MathCore::Constants::kPi * blinks)) * slowdownEnvelope
        : slowdownEnvelope;
    const Vector4 digitColor{
        std::lerp(kDigitColor.x, kSlowdownDigitColor.x, slowdownRed),
        std::lerp(kDigitColor.y, kSlowdownDigitColor.y, slowdownRed),
        std::lerp(kDigitColor.z, kSlowdownDigitColor.z, slowdownRed),
        1.0f };

    // スタミナゲージと同じ TopLeft アンカー。左上からの距離をそのまま使う。
    // 横は突入演出あけの登場で、板の右端が画面外へ抜ける距離まで左へ寄せてから戻す。
    // 縦は駅の減速で板ごと沈める。両方同時に起きても軸が違うのでそのまま足せる
    const Vector2 basePosition = cvPosition.Get();
    const Vector2 origin{
        basePosition.x - (1.0f - introReveal_) * (basePosition.x + panelWidth + kIntroMargin),
        basePosition.y + slowdownDip };

    // 板と端木は濃いめに落として緑へ寄せる。湿ったジャングルの木らしい色みにする
    const float brightness = cvBoardBrightness.Get();
    const float greenTint = cvBoardGreenTint.Get();
    const Vector4 baseBoardColor{
        brightness * (1.0f - greenTint * 0.55f),
        brightness * (1.0f + greenTint * 0.45f),
        brightness * (1.0f - greenTint * 0.95f),
        1.0f };
    // 減速中は板ごと赤へ寄せる。数字だけでは面積が小さく、視界の端では色の変化に気づけない
    const float boardTint = slowdownRed * std::clamp(cvSlowdownBoardTint.Get(), 0.0f, 1.0f);
    const Vector4 boardColor{
        std::lerp(baseBoardColor.x, kSlowdownBoardColor.x, boardTint),
        std::lerp(baseBoardColor.y, kSlowdownBoardColor.y, boardTint),
        std::lerp(baseBoardColor.z, kSlowdownBoardColor.z, boardTint),
        1.0f };

    board_->SetAnchoredPosition({ origin.x + capW, origin.y });
    board_->SetSize({ panelWidth - capW * 2.0f, panelH });
    board_->SetColor(boardColor);

    if (capLeft_) {
        capLeft_->SetAnchoredPosition({ origin.x, origin.y });
        capLeft_->SetSize({ capW, panelH });
        capLeft_->SetColor(boardColor);
    }
    if (capRight_) {
        capRight_->SetAnchoredPosition({ origin.x + panelWidth - capW, origin.y });
        capRight_->SetSize({ capW, panelH });
        capRight_->SetColor(boardColor);
    }

    // 蔦はスタミナゲージと同じ式で並べ、同じ速さで揺らす
    const float swaySpeed = cvSwaySpeed.Get();
    const float foliage = cvFoliageBrightness.Get();
    const Vector4 foliageColor{ foliage, foliage, foliage, 1.0f };
    const float vineW = kVineWidth * scale;
    const float vineH = kVineHeight * scale;
    const float vineOverhang = kVineOverhang * scale;
    const float inner = panelWidth - capW * 2.0f;
    for (std::size_t i = 0; i < vines_.size(); ++i) {
        auto* vine = vines_[i];
        if (!vine) {
            continue;
        }
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(vines_.size());
        const bool onTop = (i % 2) == 0;
        const float centerY = onTop
            ? origin.y - vineOverhang + vineH * 0.5f
            : origin.y + panelH + vineOverhang - vineH * 0.5f;
        const float phase = static_cast<float>(i) * 1.1f;
        vine->SetSize({ vineW, vineH });
        vine->SetAnchoredPosition({ origin.x + capW + inner * t, centerY });
        vine->SetUIRotation(
            (onTop ? 0.0f : MathCore::Constants::kPi) +
            std::sin(time * swaySpeed * 0.45f + phase) * 0.05f);
        vine->SetColor(foliageColor);
    }

    // 数字・小数点・単位を左から順に置く
    const float digitW = kDigitWidth * scale;
    const float digitGap = kDigitGap * scale;
    const float dotW = kDotWidth * scale;
    const float dotSideGap = kDotSideGap * scale;
    const float travel = kRollTravel * scale;
    const float centerY = origin.y + panelH * 0.5f;

    float cursor = origin.x + capW + padding;
    for (std::size_t i = 0; i < kDigitCount; ++i) {
        // 整数 3 桁のあとに小数点を挟む
        if (i == kDigitCount - 1) {
            if (dot_) {
                dot_->SetFontSize(kDigitFontSize * scale);
                dot_->SetAnchoredPosition({
                    cursor + dotSideGap + dotW * 0.5f,
                    centerY + kDotBaselineOffset * scale });
            }
            cursor += dotSideGap + dotW + dotSideGap;
        }

        Digit& digit = digits_[i];
        const float digitCenterX = cursor + digitW * 0.5f;
        const float eased = RollEase(digit.roll);

        // 縁取りも本体と同じ濃さで薄める。片方だけ残ると輪郭だけが浮いて見える
        if (digit.current) {
            digit.current->SetFontSize(kDigitFontSize * scale);
            digit.current->SetAnchoredPosition({
                digitCenterX, centerY + (1.0f - eased) * travel * digit.direction });
            digit.current->SetColor({
                digitColor.x, digitColor.y, digitColor.z, eased });
            digit.current->SetOutline({
                kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, eased }, kOutlineWidth);
        }
        if (digit.outgoing && digit.outgoing->IsActive()) {
            const float fade = 1.0f - eased;
            digit.outgoing->SetFontSize(kDigitFontSize * scale);
            digit.outgoing->SetAnchoredPosition({
                digitCenterX, centerY - eased * travel * digit.direction });
            digit.outgoing->SetColor({
                digitColor.x, digitColor.y, digitColor.z, fade });
            digit.outgoing->SetOutline({
                kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, fade }, kOutlineWidth);
        }

        cursor += digitW;
        if (i + 2 < kDigitCount) {
            cursor += digitGap;
        }
    }

    if (unit_) {
        unit_->SetFontSize(kUnitFontSize * scale);
        unit_->SetAnchoredPosition({ cursor + kUnitGap * scale, centerY });
    }

    // 沈み込みに合わせて板ごと動かしたいので、位置は原点（origin）を基準にする
    LayoutDropLabel(origin, panelWidth, panelH, scale);
}

void GameComponents::SpeedGaugeUIComponent::LayoutDropLabel(
    const Vector2& anchor, float panelWidth, float panelHeight, float scale)
{
    if (!dropLabel_) {
        return;
    }

    const float phase = GetSlowdownProgress();
    if (phase >= 1.0f) {
        if (dropLabel_->IsActive()) {
            dropLabel_->SetActive(false);
        }
        return;
    }

    // 板の下端から、時間とともに下へ落ちていく。動く向きが「下がった」を言う
    const float fall = RollEase(phase) * cvSlowdownDropFall.Get() * scale;
    // 出た瞬間だけ大きく、すぐ収まる。板から弾き出されたように見せる
    const float punch = 1.0f + kDropLabelPunch * (1.0f - std::min(phase / kDropLabelPunchSpan, 1.0f));
    // 後半だけ薄くする。頭から薄め始めると、読み取る前に消えてしまう
    const float alpha = (phase <= kDropLabelHoldPhase)
        ? 1.0f
        : 1.0f - (phase - kDropLabelHoldPhase) / (1.0f - kDropLabelHoldPhase);

    dropLabel_->SetFontSize(cvSlowdownDropFontSize.Get() * scale * punch);
    dropLabel_->SetAnchoredPosition({
        anchor.x + panelWidth * 0.5f,
        anchor.y + panelHeight + kDropLabelGap * scale + fall });
    dropLabel_->SetColor({
        kDropLabelColor.x, kDropLabelColor.y, kDropLabelColor.z, alpha });
    dropLabel_->SetOutline(
        { kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, alpha }, kOutlineWidth);
    if (!dropLabel_->IsActive()) {
        dropLabel_->SetActive(true);
    }
}

#ifdef USE_IMGUI
bool GameComponents::SpeedGaugeUIComponent::DrawInspector()
{
    const bool changed = CVarUI::DrawTree("Game.SpeedGauge");
    UI::Hint("変更は CVars.json へ自動保存されます。");
    ImGui::Separator();
    ImGui::Text("表示: %.1f km/h", displayedKilometersPerHour_);
    if (train_) {
        ImGui::TextDisabled(
            "速度: %.3f マス/秒（最低 %.3f）",
            train_->GetMoveSpeed(), train_->GetMinMoveSpeed());
    }
    return changed;
}
#endif
