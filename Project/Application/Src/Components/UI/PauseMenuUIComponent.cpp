#include "pch.h"
#include "PauseMenuUIComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Math/Easing/EasingUtil.h"
#include "Math/Vector/Vector4.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/CVarPanel.h"
#endif

using namespace CoreEngine;

namespace
{
    // ───────────────────────────────────────────────────────────────
    // テクスチャ
    // 版下は Project/Build/Scripts/gen_pause_tex.py。
    // ART_SCALE を変えたら下の寸法もまとめて直すこと
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
    /// 暗幕の単色。伸ばしてもドットが崩れない
    /// @note Models/Box/white1x1.png はアルファチャンネルの無い RGB 画像なので使えない
    ///       （乗算アルファが 0 のまま描かれず、暗幕が効かなかった）
    constexpr const char* kTexDim = "Application/Assets/Textures/Pause/dim.png";

    // ───────────────────────────────────────────────────────────────
    // テクスチャの実寸（gen_pause_tex.py の出力と一致させること）
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
    // 配置（基準解像度 1920x1080。x は画面中央から、y は上端から）
    // ───────────────────────────────────────────────────────────────
    constexpr float kCanvasWidth = 1920.0f;   ///< 基準解像度（変更禁止）
    constexpr float kCanvasHeight = 1080.0f;
    constexpr float kScreenHalfWidth = kCanvasWidth * 0.5f;
    constexpr float kBoardWidth = 900.0f;
    constexpr float kBoardTop = 176.0f;
    constexpr float kBoardHeight = kPlankHeight * 2.0f;
    constexpr float kItemWidth = 660.0f;
    constexpr float kItemFirstTop = 430.0f;
    constexpr float kItemPitch = 130.0f;
    constexpr float kItemRopeX = 200.0f;         ///< 木札を吊るす蔦の左右位置
    constexpr float kCursorOffsetX = -(kItemWidth * 0.5f) - 82.0f;
    /// 看板を吊るす蔦の x。天井から下ろす
    constexpr float kHangingVineX[] = {
        -420.0f, -300.0f, -160.0f, 0.0f, 160.0f, 300.0f, 420.0f };

    // 画面右上に常設するポーズの操作ヒント。
    // 板幅は固定せず、今出している文言の実寸から決める（HudPlankWidth）。
    // ESC とゲームパッドで文言の長さが違うので、固定幅にすると片方が間延びする
    constexpr float kHudTop = 40.0f;
    /// 文字の左右に空ける幅。木口（kCapWidth）に文字が乗らない値にすること
    constexpr float kHudTextPadding = 56.0f;
    /// 文字を取れなかったときの下限。木口 2 つぶんは要る
    constexpr float kHudMinWidth = 200.0f;
    /// 画面右端から板の右端まで。板幅が変わっても右肩の位置は動かない
    constexpr float kHudRightMargin = 48.0f;
    /// 登場アニメーションで引っ込めるときに、板の左端を画面外へ出しておく余白 [px]
    constexpr float kIntroMargin = 48.0f;

    /// 落ちてくる前に待機している高さ。画面外へ完全に出ていること
    constexpr float kDropDistance = 1300.0f;
    /// 木札が看板から遅れて降りる間隔
    constexpr float kItemDelay = 0.06f;

    // ───────────────────────────────────────────────────────────────
    // 文字
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTitleText = "ぽーずちゅう";
    constexpr const char* kItemTexts[] = { "つづける", "さいしょから", "タイトルへ" };
    constexpr const char* kHudKeyboard = "ESC ぽーず";
    constexpr const char* kHudGamepad = "START ぽーず";
    // ドット絵のフォントなので、字送りが崩れないよう 12px（1 文字の高さ）の
    // 整数倍に揃えてある。中途半端な値にするとドットがボケる
    constexpr float kTitleFontSize = 96.0f;
    constexpr float kItemFontSize = 48.0f;
    constexpr float kHudFontSize = 48.0f;
    constexpr float kOutlineWidth = 0.05f;

    // ───────────────────────────────────────────────────────────────
    // アニメーション
    // ───────────────────────────────────────────────────────────────
    constexpr float kDimFadeSeconds = 0.18f;
    constexpr float kConfirmSeconds = 0.34f;
    constexpr float kSelectFollow = 16.0f;  ///< 選択の傾き・拡大が追いつく速さ（1/秒）
    constexpr float kCursorFollow = 24.0f;  ///< 葉カーソルが札を追う速さ（1/秒）
    constexpr float kSelectTilt = -0.030f;  ///< 選択中の札の傾き [rad]
    constexpr float kSelectScale = 0.05f;   ///< 選択中の札の拡大量
    constexpr float kSelectBright = 0.20f;  ///< 選択中の札の明るさの上乗せ
    constexpr float kConfirmSquash = 0.20f; ///< 決定した瞬間に潰れる量
    constexpr float kSwaySpeed = 1.5f;      ///< 蔦と茂みが揺れるはやさ
    constexpr float kSwayAngle = 0.05f;     ///< 揺れる角度 [rad]
    constexpr float kLeafGravity = 900.0f;  ///< 舞う葉の落下加速度 [px/秒^2]
    constexpr float kLeafFadeSeconds = 0.35f;

    // ───────────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターから編集できる）
    //
    // 【色の入れ方】
    // UI はトーンマップ前のシーンバッファへ、sRGB デコード無しで描かれる。
    // つまりここへ入れた値はリニア値として扱われ、ACES → sRGB エンコードで
    // 大きく持ち上がって画面に出る。下の既定値は「画面で出したい色」から
    // 逆算したもの（gen_pause_tex.py の compensate() と同じ計算）。
    // 素直な値を入れると生成りに浮くので、明るくするときも少しずつ上げること。
    // ───────────────────────────────────────────────────────────────
    CVar<int> cvSortOrder{
        "Game.PauseMenu.SortOrder", 2000,
        "ポーズメニューの描画順（大きいほど手前）。スタミナゲージより上に置くこと",
        CVarRange{ 0.0f, 8000.0f } };

    CVar<float> cvOpenSeconds{
        "Game.PauseMenu.OpenSeconds", 0.55f,
        "看板が落ちてきて収まるまでの秒数",
        CVarRange{ 0.1f, 2.0f } };

    CVar<float> cvCloseSeconds{
        "Game.PauseMenu.CloseSeconds", 0.28f,
        "看板が跳ね上がって消えるまでの秒数",
        CVarRange{ 0.05f, 1.5f } };

    CVar<float> cvDimAlpha{
        "Game.PauseMenu.DimAlpha", 0.78f,
        "ゲーム画面を沈める暗幕の濃さ",
        CVarRange{ 0.0f, 1.0f } };

    CVar<Vector4> cvDimColor{
        "Game.PauseMenu.DimColor", { 0.006f, 0.018f, 0.008f, 1.0f },
        "暗幕の色（RGBA。A は DimAlpha 側で決める）。"
        "見せる色ではなく背景を沈める減衰なので、ここだけは逆算を掛けていない" };

    CVar<Vector4> cvTitleColor{
        "Game.PauseMenu.TitleColor", { 0.944f, 0.413f, 0.053f, 1.0f },
        "「ぽーずちゅう」の文字色。画面で (230,196,62) に見える値" };

    CVar<Vector4> cvItemColor{
        "Game.PauseMenu.ItemColor", { 0.735f, 0.546f, 0.296f, 1.0f },
        "選んでいない木札の文字色。画面で (222,210,176) に見える値" };

    CVar<Vector4> cvSelectedColor{
        "Game.PauseMenu.SelectedColor", { 1.000f, 0.546f, 0.087f, 1.0f },
        "選択中の木札の文字色。画面で (232,210,90) に見える値" };

    CVar<float> cvBrightness{
        "Game.PauseMenu.Brightness", 1.0f,
        "板と蔦の明るさ。上げるとブルームで白茶ける",
        CVarRange{ 0.2f, 2.0f } };

    CVar<int> cvConfirmLeaves{
        "Game.PauseMenu.ConfirmLeaves", 14,
        "決定したときに舞う葉の枚数",
        CVarRange{ 0.0f, 28.0f } };

    /// 露出補正の効かせすぎ防止。自動露出が振り切れても UI が黒つぶれ／白飛びしない
    constexpr float kExposureScaleMin = 0.02f;
    constexpr float kExposureScaleMax = 4.0f;

    /// @brief 中心からのずれを傾きに合わせて回す
    Vector2 RotateOffset(const Vector2& offset, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return { offset.x * c - offset.y * s, offset.x * s + offset.y * c };
    }

    /// @brief 指数で目標へ寄せる（フレームレートに依らない追従）
    float Follow(float current, float target, float rate, float deltaTime)
    {
        const float t = 1.0f - std::exp(-rate * deltaTime);
        return current + (target - current) * t;
    }

    float Rand(float minimum, float maximum)
    {
        return RandomGenerator::GetInstance().GetFloat(minimum, maximum);
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

    /// @brief 操作ヒントの板幅。今出している文言の実寸に余白を足したもの
    float HudPlankWidth(const UIText* text)
    {
        const float textWidth = text ? text->GetMeasuredSize().x : 0.0f;
        return std::max(kHudMinWidth, textWidth + kHudTextPadding * 2.0f);
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

void GameComponents::PauseMenuUIComponent::Awake()
{
    BuildParts();
}

GameComponents::PauseMenuUIComponent::Plank
GameComponents::PauseMenuUIComponent::SpawnPlank(const std::string& name, int sortOrder)
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

void GameComponents::PauseMenuUIComponent::BuildParts()
{
    auto* owner = GetOwner();
    if (!owner) {
        return;
    }

    const int baseOrder = cvSortOrder.Get();

    // オーナーは入れ物。中身は全部 Spawn で作り、経路を他のパーツと揃える
    auto* root = dynamic_cast<UIImage*>(owner);
    if (!root) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "PauseMenuUIComponent: UIImage にアタッチしてください");
        SetEnabled(false);
        return;
    }
    root->SetSize({ 1.0f, 1.0f });
    root->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });

    // 暗幕。基準解像度をそのまま覆う
    dim_ = SpawnPart(owner, kTexDim, "PauseDim", baseOrder);

    // 天井から下ろす蔦。1 本を縦に継いで画面外まで伸ばす
    for (std::size_t i = 0; i < vines_.size(); ++i) {
        vines_[i] = SpawnPart(owner, kTexVineV, "PauseVine" + std::to_string(i),
                              baseOrder + 1);
        if (vines_[i]) {
            vines_[i]->SetSize({ kVineWidth, kVineHeight });
        }
    }
    // 看板の上を這わせる蔦。模様があるので伸ばさず並べて繋ぐ
    for (std::size_t i = 0; i < creepers_.size(); ++i) {
        creepers_[i] = SpawnPart(owner, kTexVineH, "PauseCreeper" + std::to_string(i),
                                 baseOrder + 6);
        if (creepers_[i]) {
            creepers_[i]->SetSize({ kCreeperWidth, kCreeperHeight });
        }
    }
    // 葉の茂み。看板の四隅と画面の四隅（ジャングルが入り込んでくる）
    for (std::size_t i = 0; i < foliage_.size(); ++i) {
        foliage_[i] = SpawnPart(owner, kTexFoliage, "PauseFoliage" + std::to_string(i),
                                baseOrder + (i < 4 ? 7 : 1));
        if (foliage_[i]) {
            foliage_[i]->SetSize({ kFoliageWidth, kFoliageHeight });
        }
    }

    board_ = SpawnPlank("PauseBoardUpper", baseOrder + 2);
    boardLower_ = SpawnPlank("PauseBoardLower", baseOrder + 2);

    for (std::size_t i = 0; i < kItemCount; ++i) {
        Item& item = items_[i];
        const std::string name = "PauseItem" + std::to_string(i);
        item.ropeLeft = SpawnPart(owner, kTexVineV, name + "RopeL", baseOrder + 1);
        item.ropeRight = SpawnPart(owner, kTexVineV, name + "RopeR", baseOrder + 1);
        item.plank = SpawnPlank(name, baseOrder + 3);
        for (std::size_t k = 0; k < item.creepers.size(); ++k) {
            item.creepers[k] = SpawnPart(
                owner, kTexVineH, name + "Creeper" + std::to_string(k), baseOrder + 6);
            if (item.creepers[k]) {
                item.creepers[k]->SetSize({ kCreeperWidth, kCreeperHeight });
            }
        }
    }

    cursor_ = SpawnPart(owner, kTexCursor, "PauseCursor", baseOrder + 7);
    if (cursor_) {
        cursor_->SetSize({ kCursorWidth, kCursorHeight });
    }

    // 舞う葉。使い回すので最初に全部作っておく
    for (std::size_t i = 0; i < leaves_.size(); ++i) {
        leaves_[i].image = SpawnPart(
            owner, (i % 2 == 0) ? kTexLeafM : kTexLeafS,
            "PauseLeaf" + std::to_string(i), baseOrder + 8);
    }

    // 画面右上に常設するポーズの操作ヒント（板 1 枚）
    hudPlank_ = SpawnPlank("PauseHudPlank", baseOrder - 2);
    hudLeaf_ = SpawnPart(owner, kTexLeafM, "PauseHudLeaf", baseOrder - 1);
    if (hudLeaf_) {
        hudLeaf_->SetSize({ kLeafWidth, kLeafHeight });
    }

    // 文字。使う字だけを焼くので、文言を変えたらこの並びも足すこと
    auto* engine = owner->GetEngineSystem();
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    if (fontManager) {
        MsdfFontDesc fontDesc;
        // ドット絵の UI に合わせてピクセルフォントを使う
        fontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
        fontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
        fontDesc.charsetUtf8 =
            std::string(kTitleText) + kItemTexts[0] + kItemTexts[1] + kItemTexts[2]
            + kHudKeyboard + kHudGamepad;

        if (auto* font = fontManager->Acquire(fontDesc)) {
            title_ = SpawnText(owner, font, kTitleText, "PauseTitle",
                               kTitleFontSize, baseOrder + 5);
            for (std::size_t i = 0; i < kItemCount; ++i) {
                items_[i].text = SpawnText(
                    owner, font, kItemTexts[i], "PauseItemText" + std::to_string(i),
                    kItemFontSize, baseOrder + 5);
            }
            hudText_ = SpawnText(owner, font, kHudKeyboard, "PauseHudText",
                                 kHudFontSize, baseOrder - 1);
        }
    }
    if (!title_) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "PauseMenuUIComponent: フォントを取れなかったので板だけを出します");
    }

    built_ = true;
    SetHudActive(true);
}

// ───────────────────────────────────────────────────────────────────
// 開閉
// ───────────────────────────────────────────────────────────────────

void GameComponents::PauseMenuUIComponent::Open()
{
    if (!built_ || IsOpen()) {
        return;
    }
    phase_ = Phase::Opening;
    phaseTimer_ = 0.0f;
    selection_ = 0;
    confirmTimer_ = 0.0f;
    for (auto& item : items_) {
        item.select = 0.0f;
    }
    items_[selection_].select = 1.0f;
    cursorY_ = kItemFirstTop + kPlankHeight * 0.5f;
    SetMenuActive(true);
    SetHudActive(false);
    ApplyLayout(0.0f);
}

void GameComponents::PauseMenuUIComponent::Close()
{
    if (!built_ || !IsOpen()) {
        return;
    }
    phase_ = Phase::Closing;
    phaseTimer_ = 0.0f;
}

void GameComponents::PauseMenuUIComponent::MoveSelection(int delta)
{
    if (!built_ || delta == 0) {
        return;
    }
    const int count = static_cast<int>(kItemCount);
    int next = static_cast<int>(selection_) + delta;
    next = ((next % count) + count) % count;   // 端で折り返す
    selection_ = static_cast<std::size_t>(next);

    // カーソルが移った先で葉を少しだけ散らす
    const float top = kItemFirstTop + kItemPitch * static_cast<float>(selection_);
    BurstLeaves({ kCursorOffsetX, top + kPlankHeight * 0.5f }, 3, 260.0f);
}

void GameComponents::PauseMenuUIComponent::PlayConfirm()
{
    confirmTimer_ = kConfirmSeconds;
    const float top = kItemFirstTop + kItemPitch * static_cast<float>(selection_);
    BurstLeaves({ 0.0f, top + kPlankHeight * 0.5f },
                std::max(0, cvConfirmLeaves.Get()), 700.0f);
}

void GameComponents::PauseMenuUIComponent::SetExposureScale(float scale)
{
    exposureScale_ = std::clamp(scale, kExposureScaleMin, kExposureScaleMax);
}

/// @note 上限を 2 まで許すのは、EaseOutBack を通した「行き過ぎ」をそのまま活かすため
void GameComponents::PauseMenuUIComponent::SetIntroReveal(float reveal)
{
    introReveal_ = std::clamp(reveal, 0.0f, 2.0f);
}

void GameComponents::PauseMenuUIComponent::SetHintForGamepad(bool connected)
{
    gamepadHint_ = connected;
    if (hudText_) {
        hudText_->SetText(connected ? kHudGamepad : kHudKeyboard);
    }
}

// ───────────────────────────────────────────────────────────────────
// 更新
// ───────────────────────────────────────────────────────────────────

void GameComponents::PauseMenuUIComponent::Tick(float unscaledDeltaTime)
{
    if (!built_) {
        return;
    }
    swayTimer_ += unscaledDeltaTime;

    // 葉はメニューが消えたあとも飛び続ける（閉じた瞬間に消えると唐突）
    UpdateLeaves(unscaledDeltaTime);

    if (phase_ == Phase::Hidden) {
        if (!hudActive_) {
            SetHudActive(true);
        }
        // 画面右上のヒントだけ置き直す。
        // 突入演出あけの登場では、板の左端が画面外へ抜ける距離まで右へ寄せてから戻す。
        const float sway = std::sin(swayTimer_ * kSwaySpeed) * kSwayAngle * 0.5f;
        const float hudWidth = HudPlankWidth(hudText_);
        const float hudCenterX = kScreenHalfWidth - hudWidth * 0.5f - kHudRightMargin;
        const float hudX = hudCenterX + (1.0f - introReveal_)
            * (kScreenHalfWidth - hudCenterX + hudWidth * 0.5f + kIntroMargin);
        for (auto* image : { hudPlank_.mid, hudPlank_.capLeft, hudPlank_.capRight }) {
            if (image) {
                const float b = cvBrightness.Get() * exposureScale_;
                image->SetColor({ b, b, b, 1.0f });
            }
        }
        PlacePlank(hudPlank_, { hudX, kHudTop + kPlankHeight * 0.5f },
                   hudWidth, 1.0f, 1.0f, sway);
        const float bright = cvBrightness.Get() * exposureScale_;
        if (hudText_) {
            const Vector4 c = cvItemColor.Get();
            hudText_->SetAnchoredPosition({ hudX, kHudTop + kPlankHeight * 0.5f });
            hudText_->SetUIRotation(sway);
            hudText_->SetColor({ c.x * exposureScale_, c.y * exposureScale_,
                                 c.z * exposureScale_, c.w });
        }
        if (hudLeaf_) {
            hudLeaf_->SetAnchoredPosition(
                { hudX - hudWidth * 0.5f + 6.0f, kHudTop + 4.0f });
            hudLeaf_->SetUIRotation(sway - 0.5f);
            hudLeaf_->SetColor({ bright, bright, bright, 1.0f });
        }
        return;
    }

    phaseTimer_ += unscaledDeltaTime;
    confirmTimer_ = std::max(0.0f, confirmTimer_ - unscaledDeltaTime);

    if (phase_ == Phase::Opening) {
        // いちばん遅い木札が着地しきってから操作を受ける
        const float settled = cvOpenSeconds.Get()
            + kItemDelay * static_cast<float>(kItemCount);
        if (phaseTimer_ >= settled) {
            phase_ = Phase::Idle;
        }
    } else if (phase_ == Phase::Closing && phaseTimer_ >= cvCloseSeconds.Get()) {
        phase_ = Phase::Hidden;
        SetMenuActive(false);
        return;
    }

    // 選択の度合いを目標へ寄せる。切り替えた瞬間に飛ばず、傾きが滑る
    for (std::size_t i = 0; i < kItemCount; ++i) {
        const float target = (i == selection_ && phase_ != Phase::Closing) ? 1.0f : 0.0f;
        items_[i].select = Follow(items_[i].select, target, kSelectFollow, unscaledDeltaTime);
    }

    ApplyLayout(unscaledDeltaTime);
}

float GameComponents::PauseMenuUIComponent::DropProgress(float delaySeconds) const
{
    switch (phase_) {
    case Phase::Opening: {
        const float duration = std::max(0.01f, cvOpenSeconds.Get());
        const float t = std::clamp((phaseTimer_ - delaySeconds) / duration, 0.0f, 1.0f);
        // 行き過ぎてから戻る。蔦で吊った板が着地して弾む手応え
        return EasingUtil::Apply(t, EasingUtil::Type::EaseOutBack);
    }
    case Phase::Idle:
        return 1.0f;
    case Phase::Closing: {
        const float duration = std::max(0.01f, cvCloseSeconds.Get());
        const float t = std::clamp(phaseTimer_ / duration, 0.0f, 1.0f);
        // 予備動作で一度沈んでから跳ね上がる
        return 1.0f - EasingUtil::Apply(t, EasingUtil::Type::EaseInBack);
    }
    default:
        return 0.0f;
    }
}

void GameComponents::PauseMenuUIComponent::PlaceRope(
    UIImage* rope, float centerX, float topY, float length)
{
    if (!rope) {
        return;
    }
    const float clamped = std::max(0.0f, length);
    rope->SetAnchor(UIAnchor::TopCenter);
    rope->SetPivot({ 0.5f, 0.5f });
    rope->SetUIRotation(0.0f);
    rope->SetSize({ kVineWidth, clamped });
    rope->SetAnchoredPosition({ centerX, topY + clamped * 0.5f });
    const float bright = cvBrightness.Get();
    rope->SetColor({ bright, bright, bright, 1.0f });
}

void GameComponents::PauseMenuUIComponent::PlacePlank(
    const Plank& plank, const Vector2& center,
    float width, float scale, float scaleY, float angle)
{
    const float w = width * scale;
    const float h = kPlankHeight * scale * scaleY;
    const float capW = kCapWidth * scale;
    const float midW = std::max(0.0f, w - capW * 2.0f);
    const float capOffset = (w - capW) * 0.5f;

    struct Part { UIImage* image; float offsetX; float sizeX; };
    const Part parts[] = {
        { plank.mid,      0.0f,       midW },
        { plank.capLeft,  -capOffset, capW },
        { plank.capRight, capOffset,  capW },
    };

    for (const auto& part : parts) {
        if (!part.image) {
            continue;
        }
        const Vector2 offset = RotateOffset({ part.offsetX, 0.0f }, angle);
        part.image->SetAnchor(UIAnchor::TopCenter);
        part.image->SetPivot({ 0.5f, 0.5f });
        part.image->SetAnchoredPosition({ center.x + offset.x, center.y + offset.y });
        part.image->SetSize({ part.sizeX, h });
        part.image->SetUIRotation(angle);
    }
}

void GameComponents::PauseMenuUIComponent::ApplyLayout(float unscaledDeltaTime)
{
    // 自動露出のぶんを打ち消す。掛けないと明るい場所と暗い場所で
    // メニューの色が変わってしまう（このプロジェクトは自動露出が有効）
    const float bright = cvBrightness.Get() * exposureScale_;
    const Vector4 plain{ bright, bright, bright, 1.0f };

    // ---- 暗幕 ----
    float dimT = 1.0f;
    if (phase_ == Phase::Opening) {
        dimT = std::clamp(phaseTimer_ / kDimFadeSeconds, 0.0f, 1.0f);
    } else if (phase_ == Phase::Closing) {
        dimT = 1.0f - std::clamp(phaseTimer_ / std::max(0.01f, cvCloseSeconds.Get()),
                                 0.0f, 1.0f);
    }
    if (dim_) {
        dim_->SetAnchoredPosition({ 0.0f, kCanvasHeight * 0.5f });
        dim_->SetSize({ kCanvasWidth, kCanvasHeight });
        Vector4 dimColor = cvDimColor.Get();
        dimColor.w = cvDimAlpha.Get() * dimT;
        dim_->SetColor(dimColor);
    }

    // ---- 看板（板 2 枚） ----
    const float boardDrop = DropProgress(0.0f);
    const float boardTop = kBoardTop - (1.0f - boardDrop) * kDropDistance;
    const float boardBottom = boardTop + kBoardHeight;

    PlacePlank(board_, { 0.0f, boardTop + kPlankHeight * 0.5f },
               kBoardWidth, 1.0f, 1.0f, 0.0f);
    PlacePlank(boardLower_, { 0.0f, boardTop + kPlankHeight * 1.5f },
               kBoardWidth, 1.0f, 1.0f, 0.0f);
    for (auto* image : { board_.mid, board_.capLeft, board_.capRight,
                         boardLower_.mid, boardLower_.capLeft, boardLower_.capRight }) {
        if (image) { image->SetColor(plain); }
    }
    if (title_) {
        const Vector4 c = cvTitleColor.Get();
        title_->SetAnchoredPosition({ 0.0f, boardTop + kBoardHeight * 0.5f });
        title_->SetColor({ c.x * exposureScale_, c.y * exposureScale_,
                           c.z * exposureScale_, c.w });
    }

    // ---- 天井から下ろす蔦。看板の上端まで必ず届かせる ----
    const std::size_t segments = kVineSegmentCount;
    for (std::size_t v = 0; v < kHangingVineCount; ++v) {
        // 本ごとに揺れの位相をずらすと、まとめて動いて見えない
        const float sway = std::sin(swayTimer_ * kSwaySpeed + static_cast<float>(v) * 1.7f)
            * kSwayAngle;
        for (std::size_t s = 0; s < segments; ++s) {
            auto* image = vines_[v * segments + s];
            if (!image) {
                continue;
            }
            const float centerY = boardTop - kVineHeight * (static_cast<float>(s) + 0.5f);
            // 下ほど大きく振れる。付け根は動かない
            const float amount = sway * (1.0f - static_cast<float>(s) / segments);
            image->SetAnchoredPosition(
                { kHangingVineX[v] + amount * 40.0f, centerY });
            image->SetUIRotation(amount);
            image->SetSize({ kVineWidth, kVineHeight });
            image->SetColor(plain);
        }
    }

    // ---- 看板を這う蔦。並べて繋ぐ（伸ばすと模様が崩れる） ----
    // 前半を上の縁、後半を下の縁へ。板幅ぴったりには割り切れないので、
    // 左右へ均等にはみ出させる（蔦が板から垂れているように見える）
    const std::size_t creeperPerRow = kBoardCreeperCount / 2;
    const float creeperLeft =
        -kCreeperWidth * static_cast<float>(creeperPerRow) * 0.5f + kCreeperWidth * 0.5f;
    for (std::size_t i = 0; i < creepers_.size(); ++i) {
        if (!creepers_[i]) {
            continue;
        }
        const std::size_t column = i % creeperPerRow;
        const bool lowerRow = (i >= creeperPerRow);
        creepers_[i]->SetAnchoredPosition(
            { creeperLeft + kCreeperWidth * static_cast<float>(column),
              lowerRow ? boardBottom + 2.0f : boardTop - 6.0f });
        // 下の縁は上下を返して、板を挟んでいるように見せる
        creepers_[i]->SetUIRotation(lowerRow ? 3.14159265f : 0.0f);
        creepers_[i]->SetColor(plain);
    }

    // ---- 葉の茂み ----
    // 0-3: 看板の四隅（看板と一緒に落ちてくる）／ 4-7: 画面の四隅（動かない）
    const float shoulderX = kBoardWidth * 0.5f - 40.0f;
    const struct { float x; float y; float rotation; } boardFoliage[] = {
        { -shoulderX, boardTop + 8.0f,      -0.25f },
        {  shoulderX, boardTop + 8.0f,       0.25f },
        { -shoulderX, boardBottom - 12.0f,   2.9f  },
        {  shoulderX, boardBottom - 12.0f,  -2.9f  },
    };
    for (std::size_t i = 0; i < 4; ++i) {
        if (!foliage_[i]) {
            continue;
        }
        const float sway = std::sin(swayTimer_ * kSwaySpeed + static_cast<float>(i)) * kSwayAngle;
        foliage_[i]->SetAnchoredPosition({ boardFoliage[i].x, boardFoliage[i].y });
        foliage_[i]->SetUIRotation(boardFoliage[i].rotation + sway);
        foliage_[i]->SetSize({ kFoliageWidth, kFoliageHeight });
        foliage_[i]->SetColor(plain);
    }
    const struct { float x; float y; float rotation; } cornerFoliage[] = {
        { -kScreenHalfWidth + 70.0f,  70.0f,   -0.6f },
        {  kScreenHalfWidth - 70.0f,  70.0f,    0.6f },
        { -kScreenHalfWidth + 70.0f, 1010.0f,   2.5f },
        {  kScreenHalfWidth - 70.0f, 1010.0f,  -2.5f },
    };
    for (std::size_t i = 0; i < 4; ++i) {
        auto* image = foliage_[4 + i];
        if (!image) {
            continue;
        }
        const float sway = std::sin(swayTimer_ * kSwaySpeed * 0.7f + static_cast<float>(i) * 2.1f)
            * kSwayAngle;
        // 暗幕と一緒に現れる。ジャングルが画面へ入り込んでくる見せ方
        image->SetAnchoredPosition({ cornerFoliage[i].x, cornerFoliage[i].y });
        image->SetUIRotation(cornerFoliage[i].rotation + sway);
        image->SetSize({ kFoliageWidth * dimT, kFoliageHeight * dimT });
        image->SetColor(plain);
    }

    // ---- 木札 ----
    // 決定した瞬間だけ縦に潰して跳ね返す（押した手応え）
    float confirmScaleY = 1.0f;
    float confirmScaleX = 1.0f;
    if (confirmTimer_ > 0.0f) {
        const float u = 1.0f - confirmTimer_ / kConfirmSeconds;
        const float bump = std::sin(u * 3.14159265f);
        confirmScaleY = 1.0f - kConfirmSquash * bump;
        confirmScaleX = 1.0f + kConfirmSquash * 0.5f * bump;
    }

    float previousBottom = boardBottom;
    float selectedCenterY = cursorY_;

    for (std::size_t i = 0; i < kItemCount; ++i) {
        Item& item = items_[i];
        const float drop = DropProgress(kItemDelay * static_cast<float>(i + 1));
        const float top = kItemFirstTop + kItemPitch * static_cast<float>(i)
            - (1.0f - drop) * kDropDistance;
        const float centerY = top + kPlankHeight * 0.5f;

        const bool isSelected = (i == selection_);
        const float scale = 1.0f + kSelectScale * item.select;
        const float angle = kSelectTilt * item.select;
        const float scaleY = isSelected ? confirmScaleY : 1.0f;
        const float scaleX = isSelected ? confirmScaleX : 1.0f;

        PlacePlank(item.plank, { 0.0f, centerY },
                   kItemWidth * scaleX, scale, scaleY, angle);

        // 蔦は 1 つ上の板の下端と、この札の上端を繋ぐ
        PlaceRope(item.ropeLeft, -kItemRopeX, previousBottom, top - previousBottom);
        PlaceRope(item.ropeRight, kItemRopeX, previousBottom, top - previousBottom);
        previousBottom = top + kPlankHeight;

        // 札の上を這う蔦。文字に被らないよう左右へ寄せる
        for (std::size_t k = 0; k < item.creepers.size(); ++k) {
            if (!item.creepers[k]) {
                continue;
            }
            const float side = (k == 0) ? -1.0f : 1.0f;
            const Vector2 offset = RotateOffset(
                { side * (kItemWidth * 0.5f - kCreeperWidth * 0.5f) * scale,
                  -kPlankHeight * 0.5f * scale }, angle);
            item.creepers[k]->SetAnchoredPosition({ offset.x, centerY + offset.y });
            item.creepers[k]->SetUIRotation(angle);
            item.creepers[k]->SetColor(plain);
        }

        if (item.text) {
            item.text->SetAnchoredPosition({ 0.0f, centerY });
            item.text->SetUIRotation(angle);
            item.text->SetFontSize(kItemFontSize * scale);

            const Vector4 base = cvItemColor.Get();
            const Vector4 sel = cvSelectedColor.Get();
            const float t = item.select;
            const float e = exposureScale_;
            item.text->SetColor({
                (base.x + (sel.x - base.x) * t) * e,
                (base.y + (sel.y - base.y) * t) * e,
                (base.z + (sel.z - base.z) * t) * e,
                base.w + (sel.w - base.w) * t });
        }

        // 板の明るさも選択で少しだけ上げる（傾きだけだと分かりにくい）
        const float lit = bright * (1.0f + kSelectBright * item.select);
        for (auto* image : { item.plank.mid, item.plank.capLeft, item.plank.capRight }) {
            if (image) {
                image->SetColor({ lit, lit, lit, 1.0f });
            }
        }

        if (isSelected) {
            selectedCenterY = centerY;
        }
    }

    // ---- 葉のカーソル。札から札へ滑って移る ----
    if (cursor_) {
        cursorY_ = Follow(cursorY_, selectedCenterY, kCursorFollow, unscaledDeltaTime);
        cursor_->SetAnchoredPosition({ kCursorOffsetX, cursorY_ });
        cursor_->SetUIRotation(std::sin(swayTimer_ * 3.0f) * 0.08f);
        cursor_->SetSize({ kCursorWidth, kCursorHeight });
        cursor_->SetColor(plain);
    }

}

// ───────────────────────────────────────────────────────────────────
// 舞う葉
// ───────────────────────────────────────────────────────────────────

void GameComponents::PauseMenuUIComponent::BurstLeaves(
    const Vector2& origin, int count, float power)
{
    int spawned = 0;
    for (auto& leaf : leaves_) {
        if (spawned >= count) {
            break;
        }
        if (leaf.life > 0.0f || !leaf.image) {
            continue;   // まだ飛んでいる葉は横取りしない
        }
        // y は下向きなので、上へ飛ばすには sin が負になる角度を選ぶ
        const float angle = Rand(-2.6f, -0.5f);
        const float speed = power * Rand(0.45f, 1.0f);
        leaf.position = { origin.x + Rand(-90.0f, 90.0f), origin.y + Rand(-24.0f, 24.0f) };
        leaf.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        leaf.rotation = Rand(-3.14f, 3.14f);
        leaf.spin = Rand(-7.0f, 7.0f);
        leaf.lifeSpan = Rand(0.7f, 1.15f);
        leaf.life = leaf.lifeSpan;
        leaf.size = Rand(0.8f, 1.35f);
        leaf.image->SetActive(true);
        ++spawned;
    }
}

void GameComponents::PauseMenuUIComponent::UpdateLeaves(float unscaledDeltaTime)
{
    const float bright = cvBrightness.Get() * exposureScale_;
    for (auto& leaf : leaves_) {
        if (!leaf.image || leaf.life <= 0.0f) {
            continue;
        }
        leaf.life -= unscaledDeltaTime;
        if (leaf.life <= 0.0f) {
            leaf.life = 0.0f;
            leaf.image->SetActive(false);
            continue;
        }
        leaf.velocity.y += kLeafGravity * unscaledDeltaTime;
        leaf.velocity.x *= 0.99f;   // 空気抵抗。横へ飛びすぎない
        leaf.position.x += leaf.velocity.x * unscaledDeltaTime;
        leaf.position.y += leaf.velocity.y * unscaledDeltaTime;
        leaf.rotation += leaf.spin * unscaledDeltaTime;

        const float alpha = std::clamp(leaf.life / kLeafFadeSeconds, 0.0f, 1.0f);
        leaf.image->SetAnchoredPosition(leaf.position);
        leaf.image->SetUIRotation(leaf.rotation);
        leaf.image->SetSize({ kLeafWidth * leaf.size, kLeafHeight * leaf.size });
        leaf.image->SetColor({ bright, bright, bright, alpha });
    }
}

// ───────────────────────────────────────────────────────────────────
// 表示切り替え
// ───────────────────────────────────────────────────────────────────

void GameComponents::PauseMenuUIComponent::SetMenuActive(bool active)
{
    auto apply = [active](UIImage* image) { if (image) { image->SetActive(active); } };
    auto applyText = [active](UIText* text) { if (text) { text->SetActive(active); } };
    auto applyPlank = [&apply](const Plank& plank) {
        apply(plank.mid); apply(plank.capLeft); apply(plank.capRight);
    };

    apply(dim_);
    applyPlank(board_);
    applyPlank(boardLower_);
    apply(cursor_);
    applyText(title_);
    for (auto* image : vines_)    { apply(image); }
    for (auto* image : creepers_) { apply(image); }
    for (auto* image : foliage_)  { apply(image); }
    for (auto& item : items_) {
        applyPlank(item.plank);
        apply(item.ropeLeft);
        apply(item.ropeRight);
        for (auto* image : item.creepers) { apply(image); }
        applyText(item.text);
    }
}

void GameComponents::PauseMenuUIComponent::SetHudActive(bool active)
{
    hudActive_ = active;
    for (auto* image : { hudPlank_.mid, hudPlank_.capLeft, hudPlank_.capRight, hudLeaf_ }) {
        if (image) { image->SetActive(active); }
    }
    if (hudText_) {
        hudText_->SetActive(active);
    }
}

// ───────────────────────────────────────────────────────────────────
// インスペクター
// ───────────────────────────────────────────────────────────────────

#ifdef USE_IMGUI
bool GameComponents::PauseMenuUIComponent::DrawInspector()
{
    const bool changed = CVarUI::DrawTree("Game.PauseMenu");
    UI::Hint("変更は CVars.json へ自動保存されます。絵の版下は gen_pause_tex.py。");
    ImGui::Separator();

    const char* phaseName = "非表示";
    switch (phase_) {
    case Phase::Opening: phaseName = "降下中"; break;
    case Phase::Idle:    phaseName = "操作待ち"; break;
    case Phase::Closing: phaseName = "退場中"; break;
    default: break;
    }
    ImGui::Text("状態: %s / 選択: %s", phaseName, kItemTexts[selection_]);
    if (ImGui::Button(IsOpen() ? "閉じてみる" : "開いてみる")) {
        IsOpen() ? Close() : Open();
    }
    return changed;
}
#endif
