#include "pch.h"
#include "ObjectiveSignComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Math/Easing/EasingUtil.h"
#include "Math/Vector/Vector4.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace
{
    // ───────────────────────────────────────────────────────────────
    // テクスチャ（ポーズメニューと同じ版下を流用する。新規アセットは無い）
    // 版下は Project/Build/Scripts/gen_pause_tex.py
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTexPlankMid = "Application/Assets/Textures/Pause/plank_mid.png";
    constexpr const char* kTexPlankCapL = "Application/Assets/Textures/Pause/plank_cap_l.png";
    constexpr const char* kTexPlankCapR = "Application/Assets/Textures/Pause/plank_cap_r.png";
    constexpr const char* kTexVineV = "Application/Assets/Textures/Pause/vine_v.png";
    constexpr const char* kTexVineH = "Application/Assets/Textures/Pause/vine_h.png";
    constexpr const char* kTexFoliage = "Application/Assets/Textures/Pause/foliage.png";

    // ───────────────────────────────────────────────────────────────
    // テクスチャの実寸（gen_pause_tex.py の出力と一致させること）
    // ───────────────────────────────────────────────────────────────
    constexpr float kPlankHeight = 96.0f;
    constexpr float kCapWidth = 40.0f;
    constexpr float kVineWidth = 32.0f;
    constexpr float kCreeperWidth = 128.0f;
    constexpr float kCreeperHeight = 48.0f;
    constexpr float kFoliageWidth = 208.0f;
    constexpr float kFoliageHeight = 128.0f;

    // ───────────────────────────────────────────────────────────────
    // 配置（基準解像度 1920x1080。x は画面中央から、y は上端から）
    // ───────────────────────────────────────────────────────────────
    /// 出てくる前に待機している高さ。板も蔦も画面外へ完全に出ていること
    constexpr float kDropDistance = 900.0f;
    /// 看板を吊るす蔦の左右位置（板幅に対する比率）
    constexpr float kRopeXRatio = 0.33f;
    constexpr float kLabelOffsetY = 48.0f;   ///< 上の板の中心
    constexpr float kNumberOffsetY = 144.0f; ///< 下の板の中心

    // ───────────────────────────────────────────────────────────────
    // 文字
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kLabelText = "もくひょう";
    constexpr const char* kCallText = "つなげ！！";
    /// 焼く字。距離は 500 → 1000 → … と桁が伸びるので数字は 0-9 すべて要る
    constexpr const char* kCharset = "もくひょうつなげ！0123456789ｍm";
    // ドット絵のフォントなので、字送りが崩れないよう 12px（1 文字の高さ）の
    // 整数倍に揃えてある。中途半端な値にするとドットがボケる
    constexpr float kLabelFontSize = 48.0f;
    constexpr float kNumberFontSize = 84.0f;
    constexpr float kOutlineWidth = 0.05f;

    // ───────────────────────────────────────────────────────────────
    // アニメーション
    // ───────────────────────────────────────────────────────────────
    constexpr float kImpactDecay = 3.0f;      ///< 着地の揺れが収まる速さ（1/秒）
    constexpr float kImpactSpeed = 11.0f;     ///< 着地の揺れのはやさ [rad/秒]
    constexpr float kImpactAngle = 0.055f;    ///< 着地の揺れの最大角 [rad]
    constexpr float kIdleSwaySpeed = 1.4f;    ///< 吊られたままのそよぎのはやさ
    constexpr float kIdleSwayAngle = 0.012f;  ///< そよぎの角度 [rad]
    constexpr float kCallPunchSeconds = 0.26f;///< 「つなげ！！」が縮んで収まるまで
    constexpr float kCallPunchScale = 2.0f;   ///< 叩き込む瞬間の倍率
    constexpr float kCallFadeSeconds = 0.40f; ///< 消えるまで
    constexpr float kCallTilt = -0.05f;       ///< 傾き [rad]

    /// 1 フレームで進める上限 [秒]。シーン読み込み直後の跳ねで演出が飛ぶのを防ぐ
    constexpr float kMaxStepSeconds = 0.1f;

    /// 露出補正の効かせすぎ防止。自動露出が振り切れても UI が黒つぶれ／白飛びしない
    constexpr float kExposureScaleMin = 0.02f;
    constexpr float kExposureScaleMax = 4.0f;

    // ───────────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターの「ゲーム設定」に出る）
    //
    // 【色の入れ方】UI はトーンマップ前のシーンバッファへ sRGB デコード無しで描かれる。
    // ここへ入れた値はリニア値として扱われ、ACES → sRGB エンコードで大きく持ち上がる。
    // 既定値は「画面で出したい色」から逆算したもの（ポーズメニューと同じ計算）。
    // ───────────────────────────────────────────────────────────────
    CVar<int> cvSortOrder{
        "Game.ObjectiveSign.SortOrder", 1800,
        "目標看板の描画順（大きいほど手前）。ポーズメニューより下に置くこと",
        CVarRange{ 0.0f, 8000.0f } };

    CVar<float> cvBoardWidth{
        "Game.ObjectiveSign.BoardWidth", 720.0f,
        "看板の横幅 [px]（基準解像度 1920x1080）",
        CVarRange{ 200.0f, 1600.0f } };

    CVar<float> cvBoardTop{
        "Game.ObjectiveSign.Top", 132.0f,
        "収まったときの看板の上端 [px]（画面上端から）",
        CVarRange{ 0.0f, 700.0f } };

    CVar<float> cvDropSeconds{
        "Game.ObjectiveSign.DropSeconds", 0.62f,
        "看板が降りてきて収まるまでの秒数",
        CVarRange{ 0.1f, 2.0f } };

    CVar<float> cvHoldSeconds{
        "Game.ObjectiveSign.HoldSeconds", 1.90f,
        "看板が出たままそよいでいる秒数",
        CVarRange{ 0.0f, 8.0f } };

    CVar<float> cvRiseSeconds{
        "Game.ObjectiveSign.RiseSeconds", 0.34f,
        "看板が巻き上がって消えるまでの秒数",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvBrightness{
        "Game.ObjectiveSign.Brightness", 1.0f,
        "板と蔦の明るさ。上げるとブルームで白茶ける",
        CVarRange{ 0.2f, 2.0f } };

    CVar<Vector4> cvLabelColor{
        "Game.ObjectiveSign.LabelColor", { 0.735f, 0.546f, 0.296f, 1.0f },
        "「もくひょう」の文字色。画面で (222,210,176) に見える値" };

    CVar<Vector4> cvNumberColor{
        "Game.ObjectiveSign.NumberColor", { 0.944f, 0.413f, 0.053f, 1.0f },
        "距離の文字色。画面で (230,196,62) に見える値" };

    CVar<Vector4> cvCallColor{
        "Game.ObjectiveSign.CallColor", { 1.000f, 0.546f, 0.087f, 1.0f },
        "「つなげ！！」の文字色。画面で (232,210,90) に見える値" };

    CVar<float> cvCallFontSize{
        "Game.ObjectiveSign.CallFontSize", 120.0f,
        "「つなげ！！」の字の大きさ [px]",
        CVarRange{ 24.0f, 320.0f } };

    CVar<float> cvCallY{
        "Game.ObjectiveSign.CallY", 640.0f,
        "「つなげ！！」の中心の高さ [px]（画面上端から）",
        CVarRange{ 0.0f, 1080.0f } };

    CVar<float> cvCallSeconds{
        "Game.ObjectiveSign.CallSeconds", 1.60f,
        "「つなげ！！」が消えるまでの秒数（フェードを含む）",
        CVarRange{ 0.3f, 6.0f } };

    /// @brief 中心からのずれを傾きに合わせて回す
    Vector2 RotateOffset(const Vector2& offset, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { offset.x * c - offset.y * s, offset.x * s + offset.y * c };
    }

    /// @brief シリアライズ対象から外した UI をシーンへ足す
    UIImage* SpawnPart(GameObject* owner, const char* texture,
                       const std::string& name, int sortOrder)
    {
        auto* image = owner->Spawn<UIImage>();
        if (!image) {
            return nullptr;
        }
        image->Initialize(texture, name);
        image->SetSerializeEnabled(false);
        image->SetAnchor(UIAnchor::TopCenter);
        image->SetPivot({ 0.5f, 0.5f });
        image->SetSortOrder(sortOrder);
        image->SetActive(false);
        return image;
    }

    /// @brief 生成済みのフォントで UIText を 1 つ生む
    UIText* SpawnText(GameObject* owner, MsdfFont* font, const char* text,
                      const std::string& name, float fontSize, int sortOrder)
    {
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
        label->SetSortOrder(sortOrder);
        label->SetActive(false);
        return label;
    }
}

// ───────────────────────────────────────────────────────────────────
// 生成
// ───────────────────────────────────────────────────────────────────

void GameComponents::ObjectiveSignComponent::Awake()
{
    BuildParts();
}

GameComponents::ObjectiveSignComponent::Plank
GameComponents::ObjectiveSignComponent::SpawnPlankRow(const std::string& name, int sortOrder)
{
    Plank plank;
    auto* owner = GetOwner();
    if (!owner) {
        return plank;
    }
    plank.mid = SpawnPart(owner, kTexPlankMid, name + "Mid", sortOrder);
    plank.capLeft = SpawnPart(owner, kTexPlankCapL, name + "CapL", sortOrder + 1);
    plank.capRight = SpawnPart(owner, kTexPlankCapR, name + "CapR", sortOrder + 1);
    return plank;
}

void GameComponents::ObjectiveSignComponent::BuildParts()
{
    auto* owner = GetOwner();
    if (!owner) {
        return;
    }

    auto* root = dynamic_cast<UIImage*>(owner);
    if (!root) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "ObjectiveSignComponent: UIImage にアタッチしてください");
        SetEnabled(false);
        return;
    }
    // オーナーは入れ物。中身は全部 Spawn で作り、経路を他のパーツと揃える
    root->SetSize({ 1.0f, 1.0f });
    root->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });

    const int baseOrder = cvSortOrder.Get();

    // 天井から下ろす蔦。看板が降りるほど伸びるので、1 枚を縦に引き伸ばす
    for (std::size_t i = 0; i < ropes_.size(); ++i) {
        ropes_[i] = SpawnPart(owner, kTexVineV, "ObjectiveRope" + std::to_string(i),
                              baseOrder);
    }
    // 板の上を這わせる蔦。模様があるので伸ばさず並べて繋ぐ
    for (std::size_t i = 0; i < creepers_.size(); ++i) {
        creepers_[i] = SpawnPart(owner, kTexVineH, "ObjectiveCreeper" + std::to_string(i),
                                 baseOrder + 4);
        if (creepers_[i]) {
            creepers_[i]->SetSize({ kCreeperWidth, kCreeperHeight });
        }
    }
    // 板の左右の肩に乗る茂み
    for (std::size_t i = 0; i < foliage_.size(); ++i) {
        foliage_[i] = SpawnPart(owner, kTexFoliage, "ObjectiveFoliage" + std::to_string(i),
                                baseOrder + 5);
        if (foliage_[i]) {
            foliage_[i]->SetSize({ kFoliageWidth, kFoliageHeight });
        }
    }

    boardUpper_ = SpawnPlankRow("ObjectiveBoardUpper", baseOrder + 1);
    boardLower_ = SpawnPlankRow("ObjectiveBoardLower", baseOrder + 1);

    // 文字。使う字だけを焼くので、文言を変えたら kCharset も足すこと
    auto* engine = owner->GetEngineSystem();
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    if (fontManager) {
        MsdfFontDesc fontDesc;
        // ドット絵の UI に合わせてピクセルフォントを使う（ポーズメニューと同じ）
        fontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
        fontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
        fontDesc.charsetUtf8 = kCharset;

        if (auto* font = fontManager->Acquire(fontDesc)) {
            label_ = SpawnText(owner, font, kLabelText, "ObjectiveLabel",
                               kLabelFontSize, baseOrder + 3);
            number_ = SpawnText(owner, font, "0ｍ", "ObjectiveNumber",
                                kNumberFontSize, baseOrder + 3);
            call_ = SpawnText(owner, font, kCallText, "ObjectiveStartCall",
                              cvCallFontSize.Get(), baseOrder + 6);
        }
    }
    if (!number_) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "ObjectiveSignComponent: フォントを取れなかったので板だけを出します");
    }

    built_ = true;
}

// ───────────────────────────────────────────────────────────────────
// 外からの操作
// ───────────────────────────────────────────────────────────────────

void GameComponents::ObjectiveSignComponent::Show(std::uint32_t meters)
{
    if (!built_) {
        return;
    }
    meters_ = meters;
    if (number_) {
        number_->SetText(std::to_string(meters_) + "ｍ");
    }
    // 表示中に呼ばれたら頭から降り直す（目標が切り替わった合図になる）
    phase_ = Phase::Showing;
    phaseTimer_ = 0.0f;
    SetSignActive(true);
    ApplySignLayout();
}

void GameComponents::ObjectiveSignComponent::PlayStartCall()
{
    if (!built_ || !call_) {
        return;
    }
    callTimer_ = 0.0f;
    call_->SetActive(true);
    ApplyStartCall(0.0f);
}

void GameComponents::ObjectiveSignComponent::SetExposureScale(float scale)
{
    exposureScale_ = std::clamp(scale, kExposureScaleMin, kExposureScaleMax);
}

// ───────────────────────────────────────────────────────────────────
// 更新
// ───────────────────────────────────────────────────────────────────

void GameComponents::ObjectiveSignComponent::Update()
{
    if (!built_) {
        return;
    }
    // ポーズ中は止まってほしいので、実測時間ではなくゲーム内時間で数える
    const float deltaTime = std::clamp(Time::DeltaTime(), 0.0f, kMaxStepSeconds);
    swayTimer_ += deltaTime;

    if (phase_ == Phase::Showing) {
        phaseTimer_ += deltaTime;
        const float total = std::max(0.01f, cvDropSeconds.Get())
            + std::max(0.0f, cvHoldSeconds.Get())
            + std::max(0.01f, cvRiseSeconds.Get());
        if (phaseTimer_ >= total) {
            phase_ = Phase::Hidden;
            SetSignActive(false);
        } else {
            ApplySignLayout();
        }
    }

    if (callTimer_ >= 0.0f) {
        ApplyStartCall(deltaTime);
    }
}

float GameComponents::ObjectiveSignComponent::DropProgress() const
{
    const float drop = std::max(0.01f, cvDropSeconds.Get());
    const float hold = std::max(0.0f, cvHoldSeconds.Get());
    const float rise = std::max(0.01f, cvRiseSeconds.Get());

    if (phaseTimer_ < drop) {
        // 行き過ぎてから戻る。蔦で吊った板が着地して弾む手応え
        return EasingUtil::Apply(phaseTimer_ / drop, EasingUtil::Type::EaseOutBack);
    }
    if (phaseTimer_ < drop + hold) {
        return 1.0f;
    }
    const float t = std::clamp((phaseTimer_ - drop - hold) / rise, 0.0f, 1.0f);
    // 予備動作で一度沈んでから跳ね上がる
    return 1.0f - EasingUtil::Apply(t, EasingUtil::Type::EaseInBack);
}

float GameComponents::ObjectiveSignComponent::SwayAngle() const
{
    const float since = phaseTimer_ - std::max(0.01f, cvDropSeconds.Get());
    const float impact = since > 0.0f
        ? std::exp(-kImpactDecay * since) * std::sin(since * kImpactSpeed) * kImpactAngle
        : 0.0f;
    return impact + std::sin(swayTimer_ * kIdleSwaySpeed) * kIdleSwayAngle;
}

void GameComponents::ObjectiveSignComponent::PlacePlank(
    const Plank& plank, const Vector2& center, float width, float angle) const
{
    const float capOffset = (width - kCapWidth) * 0.5f;
    const float midWidth = std::max(0.0f, width - kCapWidth * 2.0f);

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
        part.image->SetSize({ part.sizeX, kPlankHeight });
        part.image->SetUIRotation(angle);
    }
}

void GameComponents::ObjectiveSignComponent::ApplySignLayout()
{
    // 自動露出のぶんを打ち消す。掛けないと昼と夜で看板の色が変わってしまう
    const float bright = cvBrightness.Get() * exposureScale_;
    const Vector4 plain{ bright, bright, bright, 1.0f };

    const float width = std::max(2.0f * kCapWidth, cvBoardWidth.Get());
    const float boardTop = cvBoardTop.Get() - (1.0f - DropProgress()) * kDropDistance;
    const float angle = SwayAngle();
    const Vector2 hinge{ 0.0f, boardTop };   // 蔦で吊っている点。ここを軸に振れる

    // ---- 天井から下ろす蔦。板が降りるほど伸びる ----
    const float ropeLength = std::max(0.0f, boardTop);
    for (std::size_t i = 0; i < ropes_.size(); ++i) {
        UIImage* rope = ropes_[i];
        if (!rope) {
            continue;
        }
        const float x = (i == 0 ? -1.0f : 1.0f) * width * kRopeXRatio;
        rope->SetSize({ kVineWidth, ropeLength });
        rope->SetAnchoredPosition({ x, ropeLength * 0.5f });
        rope->SetUIRotation(0.0f);
        rope->SetColor(plain);
    }

    // ---- 板 2 段 ----
    const Vector2 upperOffset = RotateOffset({ 0.0f, kPlankHeight * 0.5f }, angle);
    const Vector2 lowerOffset = RotateOffset({ 0.0f, kPlankHeight * 1.5f }, angle);
    PlacePlank(boardUpper_, { hinge.x + upperOffset.x, hinge.y + upperOffset.y }, width, angle);
    PlacePlank(boardLower_, { hinge.x + lowerOffset.x, hinge.y + lowerOffset.y }, width, angle);
    for (auto* image : { boardUpper_.mid, boardUpper_.capLeft, boardUpper_.capRight,
                         boardLower_.mid, boardLower_.capLeft, boardLower_.capRight }) {
        if (image) {
            image->SetColor(plain);
        }
    }

    // ---- 板の上を這う蔦 ----
    const float creeperPitch = width / static_cast<float>(creepers_.size());
    for (std::size_t i = 0; i < creepers_.size(); ++i) {
        UIImage* creeper = creepers_[i];
        if (!creeper) {
            continue;
        }
        const float x = -width * 0.5f + creeperPitch * (static_cast<float>(i) + 0.5f);
        const Vector2 offset = RotateOffset({ x, 0.0f }, angle);
        creeper->SetAnchoredPosition({ hinge.x + offset.x, hinge.y + offset.y });
        creeper->SetUIRotation(angle);
        creeper->SetColor(plain);
    }

    // ---- 肩の茂み ----
    for (std::size_t i = 0; i < foliage_.size(); ++i) {
        UIImage* leaf = foliage_[i];
        if (!leaf) {
            continue;
        }
        const float side = (i == 0 ? -1.0f : 1.0f);
        const Vector2 offset = RotateOffset({ side * (width * 0.5f - 24.0f), 6.0f }, angle);
        leaf->SetAnchoredPosition({ hinge.x + offset.x, hinge.y + offset.y });
        leaf->SetUIRotation(angle + side * 0.12f);
        leaf->SetColor(plain);
    }

    // ---- 文字 ----
    if (label_) {
        const Vector2 offset = RotateOffset({ 0.0f, kLabelOffsetY }, angle);
        label_->SetAnchoredPosition({ hinge.x + offset.x, hinge.y + offset.y });
        label_->SetUIRotation(angle);
        const Vector4 c = cvLabelColor.Get();
        label_->SetColor({ c.x * exposureScale_, c.y * exposureScale_,
                           c.z * exposureScale_, c.w });
    }
    if (number_) {
        const Vector2 offset = RotateOffset({ 0.0f, kNumberOffsetY }, angle);
        number_->SetAnchoredPosition({ hinge.x + offset.x, hinge.y + offset.y });
        number_->SetUIRotation(angle);
        const Vector4 c = cvNumberColor.Get();
        number_->SetColor({ c.x * exposureScale_, c.y * exposureScale_,
                            c.z * exposureScale_, c.w });
    }
}

void GameComponents::ObjectiveSignComponent::ApplyStartCall(float deltaTime)
{
    if (!call_) {
        callTimer_ = -1.0f;
        return;
    }
    callTimer_ += deltaTime;

    const float total = std::max(0.3f, cvCallSeconds.Get());
    if (callTimer_ >= total) {
        callTimer_ = -1.0f;
        call_->SetActive(false);
        return;
    }

    // 大きく出て、行き過ぎてから収まる
    const float punch = std::clamp(callTimer_ / kCallPunchSeconds, 0.0f, 1.0f);
    const float scale = kCallPunchScale
        + (1.0f - kCallPunchScale) * EasingUtil::Apply(punch, EasingUtil::Type::EaseOutBack);
    const float fadeStart = total - kCallFadeSeconds;
    const float alpha = callTimer_ <= fadeStart
        ? 1.0f
        : std::clamp(1.0f - (callTimer_ - fadeStart) / kCallFadeSeconds, 0.0f, 1.0f);

    call_->SetFontSize(cvCallFontSize.Get() * scale);
    call_->SetAnchoredPosition({ 0.0f, cvCallY.Get() });
    call_->SetUIRotation(kCallTilt);
    const Vector4 c = cvCallColor.Get();
    call_->SetColor({ c.x * exposureScale_, c.y * exposureScale_,
                      c.z * exposureScale_, alpha });
    // 本体と縁取りのアルファを揃える。片方だけ薄めると輪郭だけが残って浮く
    call_->SetOutline({ 0.0f, 0.0f, 0.0f, alpha }, kOutlineWidth);
}

void GameComponents::ObjectiveSignComponent::SetSignActive(bool active)
{
    for (auto* image : { boardUpper_.mid, boardUpper_.capLeft, boardUpper_.capRight,
                         boardLower_.mid, boardLower_.capLeft, boardLower_.capRight }) {
        if (image) {
            image->SetActive(active);
        }
    }
    for (auto* image : ropes_) {
        if (image) { image->SetActive(active); }
    }
    for (auto* image : creepers_) {
        if (image) { image->SetActive(active); }
    }
    for (auto* image : foliage_) {
        if (image) { image->SetActive(active); }
    }
    if (label_) { label_->SetActive(active); }
    if (number_) { number_->SetActive(active); }
}

#ifdef USE_IMGUI
bool GameComponents::ObjectiveSignComponent::DrawInspector()
{
    ImGui::Text("状態: %s", phase_ == Phase::Hidden ? "しまってある" : "出ている");
    ImGui::Text("目標: %u m", meters_);
    if (ImGui::Button("看板を出す（確認用）")) {
        Show(meters_ > 0 ? meters_ : 500u);
    }
    ImGui::SameLine();
    if (ImGui::Button("つなげ！！")) {
        PlayStartCall();
    }
    ImGui::TextWrapped(
        "見た目と時間は「ゲーム設定」の Game.ObjectiveSign.* から調整します。");
    return false;
}
#endif
