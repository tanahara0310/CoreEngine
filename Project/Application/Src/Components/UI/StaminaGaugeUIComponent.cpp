#include "pch.h"
#include "StaminaGaugeUIComponent.h"

#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include "UI/UIImage.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/CVarPanel.h"
#endif

using namespace CoreEngine;

namespace
{
    // ───────────────────────────────────────────────────────────────
    // テクスチャ（基準解像度 1920x1080 の等倍で描いてある）
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTexPip = "Application/Assets/Textures/Stamina/pip.png";
    constexpr const char* kTexPipEmpty = "Application/Assets/Textures/Stamina/pip_empty.png";
    constexpr const char* kTexBoardMid = "Application/Assets/Textures/Stamina/board_mid.png";
    constexpr const char* kTexBoardCapL = "Application/Assets/Textures/Stamina/board_cap_l.png";
    constexpr const char* kTexBoardCapR = "Application/Assets/Textures/Stamina/board_cap_r.png";
    constexpr const char* kTexLeaf = "Application/Assets/Textures/Stamina/leaf.png";
    constexpr const char* kTexVine = "Application/Assets/Textures/Stamina/vine.png";

    // ───────────────────────────────────────────────────────────────
    // 版下の寸法。テクスチャの実サイズと一致させること（伸ばすとドットがボケる）
    // ───────────────────────────────────────────────────────────────
    /// テクスチャを何倍で書き出してあるか。gen_gauge_tex.py の ART_SCALE と揃えること。
    /// 表示側で拡大するとリニアサンプラーでボケるので、大きくしたいときは
    /// テクスチャごと描き直してこの値を合わせる。
    constexpr float kArtScale = 2.0f;

    constexpr float kPipWidth = 5.0f * kArtScale;
    constexpr float kPipHeight = 15.0f * kArtScale;
    constexpr float kPipPitch = 6.0f * kArtScale;   ///< 粒 1 つぶんの送り幅
    constexpr float kBunchGap = 4.0f * kArtScale;    ///< 房と房のあいだ（＝目盛りの切れ目）
    constexpr std::size_t kPipsPerBunch = 5;
    constexpr float kCapWidth = 14.0f * kArtScale;
    constexpr float kPanelHeight = 38.0f * kArtScale;
    constexpr float kInnerPadding = 7.0f * kArtScale;
    /// 登場アニメーションで引っ込めるときに、板の右端を画面外へ出しておく余白 [px]
    constexpr float kIntroMargin = 48.0f;
    constexpr float kPipTopOffset = 11.0f * kArtScale; ///< 板の上端から粒の上端まで
    constexpr float kLeafWidth = 22.0f * kArtScale;
    constexpr float kLeafHeight = 13.0f * kArtScale;
    constexpr float kVineWidth = 52.0f * kArtScale;
    constexpr float kVineHeight = 19.0f * kArtScale;
    /// 板の縁へ絡ませる蔦の本数（上下交互に並べる）
    constexpr std::size_t kVineCount = 6;
    /// 蔦が板からはみ出す量
    constexpr float kVineOverhang = 6.0f * kArtScale;

    /// 粒を並べられる上限。スタミナ上限を上げすぎても画面が埋まらないようにする
    constexpr std::size_t kMaxPipCount = 80;

    // ───────────────────────────────────────────────────────────────
    // アニメーション
    // ───────────────────────────────────────────────────────────────
    constexpr float kGrowRate = 11.0f;    ///< 生えるはやさ（1 秒あたりの grow 変化量）
    constexpr float kEatRate = 9.0f;     ///< 食べられるはやさ
    constexpr float kGrowStagger = 0.02f;///< 連続で生えるときのずらし秒数
    constexpr float kFlashDecay = 5.0f;
    /// バナナが入ったときに、実っている粒をどこまで縮めてから伸び直させるか。
    /// 0 まで縮めると皮テクスチャへ差し替わってしまうので、実のまま残る値にしておく
    constexpr float kGainPopGrowFrom = 0.45f;
    /// 一度に弾ませる粒の上限。房 2 つぶん。これ以上はゲージ全体の弾みで見せる
    constexpr std::size_t kGainPopMaxPips = kPipsPerBunch * 2;

    // ───────────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターから編集できる）
    // ───────────────────────────────────────────────────────────────
    CVar<bool> cvEnabled{
        "Game.StaminaGauge.Enabled", true,
        "スタミナのバナナゲージを表示する" };

    CVar<Vector2> cvPosition{
        "Game.StaminaGauge.Position", { 32.0f, 44.0f },
        "画面左上を基準にしたゲージの位置 [px]（基準解像度 1920x1080）",
        CVarRange{ -2000.0f, 2000.0f } };

    CVar<float> cvScale{
        "Game.StaminaGauge.Scale", 1.0f,
        "ゲージ全体の表示倍率。1 以外にするとドットが補間されて少し甘くなる",
        CVarRange{ 0.25f, 2.0f } };

    CVar<float> cvStaminaPerPip{
        "Game.StaminaGauge.StaminaPerPip", 2.0f,
        "バナナ 1 粒が表すスタミナ量。既定 2 はレール 1 マスの基本コストと同じ",
        CVarRange{ 0.5f, 20.0f } };

    CVar<int> cvSortOrder{
        "Game.StaminaGauge.SortOrder", 900,
        "ゲージの描画順（大きいほど手前）",
        CVarRange{ 0.0f, 5000.0f } };

    CVar<float> cvSwaySpeed{
        "Game.StaminaGauge.SwaySpeed", 1.8f,
        "バナナと葉が揺れるはやさ",
        CVarRange{ 0.0f, 10.0f } };

    CVar<float> cvSwayAmplitude{
        "Game.StaminaGauge.SwayAmplitude", 0.05f,
        "バナナが揺れる角度 [rad]",
        CVarRange{ 0.0f, 0.5f } };

    CVar<float> cvSwayBob{
        "Game.StaminaGauge.SwayBob", 2.4f,
        "バナナが上下に揺れる幅 [px]",
        CVarRange{ 0.0f, 8.0f } };

    /// UI は post effect より前に合成されるため、明るい粒はブルームで白飛びする。
    /// テクスチャを描き直さずに済むよう、粒の明るさをここで下げられるようにしておく。
    CVar<float> cvPipBrightness{
        "Game.StaminaGauge.PipBrightness", 0.45f,
        "バナナの粒の明るさ。1 に近づけるとブルームで白く飛ぶ",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvFoliageBrightness{
        "Game.StaminaGauge.FoliageBrightness", 0.5f,
        "蔦と葉の明るさ。粒と同じくブルームで白く飛ぶので下げて使う",
        CVarRange{ 0.05f, 2.0f } };

    CVar<bool> cvPreviewEnabled{
        "Game.StaminaGauge.PreviewEnabled", true,
        "次の 1 マスが通常のレールより高いとき、食べられる粒を点滅で予告する" };

    // ───────────────────────────────────────────────────────────────
    // バナナが飛んで入ってきたときの反応
    // ───────────────────────────────────────────────────────────────
    CVar<float> cvGainPopDuration{
        "Game.StaminaGauge.GainPopDuration", 0.38f,
        "収穫したバナナがゲージへ入ったときに、ゲージが弾んで収まるまでの秒数",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvGainPopLift{
        "Game.StaminaGauge.GainPopLift", 6.0f,
        "バナナが入った瞬間にゲージ全体が持ち上がる量 [px]。0 で上下に動かなくなる",
        CVarRange{ 0.0f, 40.0f } };

    CVar<float> cvGainPopStretch{
        "Game.StaminaGauge.GainPopStretch", 0.18f,
        "バナナが入った瞬間に粒が縦へ伸びる量。0 で伸縮なし",
        CVarRange{ 0.0f, 1.0f } };

    /// 生えた／食べられた瞬間の発光量
    CVar<float> cvFlashStrength{
        "Game.StaminaGauge.FlashStrength", 1.4f,
        "粒が切り替わった瞬間の発光の強さ",
        CVarRange{ 0.0f, 4.0f } };

    /// @brief 生えるときだけ少し行き過ぎてから戻る（実がなる手応え）
    float GrowEase(float t)
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float inv = clamped - 1.0f;
        return 1.0f + inv * inv * (inv * 1.2f + 1.2f);
    }

    float MoveTowards(float current, float target, float maxDelta)
    {
        if (std::abs(target - current) <= maxDelta) {
            return target;
        }
        return current + std::copysign(maxDelta, target - current);
    }

    /// @brief シリアライズ対象から外した UI をシーンへ足す
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
}

void GameComponents::StaminaGaugeUIComponent::Awake()
{
    if (!hunger_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "StaminaGaugeUIComponent: Hunger が未設定です");
        SetEnabled(false);
        return;
    }
    BuildParts();
}

void GameComponents::StaminaGaugeUIComponent::BuildParts()
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
            "StaminaGaugeUIComponent: UIImage にアタッチしてください");
        SetEnabled(false);
        return;
    }
    const int baseOrder = cvSortOrder.Get();
    board_->SetAnchor(UIAnchor::TopLeft);
    board_->SetPivot({ 0.0f, 0.0f });
    board_->SetSortOrder(baseOrder);

    capLeft_ = SpawnPart(owner, kTexBoardCapL, "StaminaGaugeCapL", baseOrder + 1);
    capRight_ = SpawnPart(owner, kTexBoardCapR, "StaminaGaugeCapR", baseOrder + 1);
    for (auto* cap : { capLeft_, capRight_ }) {
        if (cap) {
            cap->SetPivot({ 0.0f, 0.0f });
            cap->SetSize({ kCapWidth, kPanelHeight });
        }
    }

    // 粒は上限ぶん作っておき、実際に使う数だけ表示する。
    // スタミナ上限を実行中に変えても作り直さずに済む。
    pips_.resize(kMaxPipCount);
    for (std::size_t i = 0; i < kMaxPipCount; ++i) {
        Pip& pip = pips_[i];
        pip.image = SpawnPart(
            owner, kTexPip, "StaminaGaugePip_" + std::to_string(i), baseOrder + 2);
        if (pip.image) {
            // 上端を軸にすると、縮めたときに下から食べられたように見える
            pip.image->SetPivot({ 0.5f, 0.0f });
            pip.image->SetSize({ kPipWidth, kPipHeight });
            pip.image->SetActive(false);
        }
        pip.grow = 0.0f;
        pip.filled = false;
        pip.showsFruit = true;
    }

    // 端木に絡ませる葉。揺れるので軸は付け根（左端）へ置く
    leaves_.reserve(2);
    for (int i = 0; i < 2; ++i) {
        auto* leaf = SpawnPart(
            owner, kTexLeaf, "StaminaGaugeLeaf_" + std::to_string(i), baseOrder + 3);
        if (leaf) {
            leaf->SetPivot({ 0.0f, 0.5f });
            leaf->SetSize({ kLeafWidth, kLeafHeight });
            leaves_.push_back(leaf);
        }
    }

    // 板の縁へ絡ませる蔦。上下交互に並べて、板が蔦に巻かれているように見せる
    vines_.reserve(kVineCount);
    for (std::size_t i = 0; i < kVineCount; ++i) {
        auto* vine = SpawnPart(
            owner, kTexVine, "StaminaGaugeVine_" + std::to_string(i), baseOrder + 4);
        if (vine) {
            vine->SetPivot({ 0.5f, 0.5f });
            vine->SetSize({ kVineWidth, kVineHeight });
            vines_.push_back(vine);
        }
    }

    built_ = true;
}

std::size_t GameComponents::StaminaGaugeUIComponent::CalculatePipCount() const
{
    const float perPip = std::max(0.5f, cvStaminaPerPip.Get());
    const float maximum = hunger_->GetMaximumHunger();
    const auto count = static_cast<std::size_t>(std::ceil(maximum / perPip));
    return std::clamp<std::size_t>(count, 1, kMaxPipCount);
}

void GameComponents::StaminaGaugeUIComponent::Update()
{
    if (!built_ || !hunger_ || !board_) {
        return;
    }

    const bool enabled = cvEnabled.Get();
    if (!enabled) {
        if (board_->IsActive()) {
            board_->SetActive(false);
            for (auto* cap : { capLeft_, capRight_ }) {
                if (cap) { cap->SetActive(false); }
            }
            for (auto& pip : pips_) {
                if (pip.image) { pip.image->SetActive(false); }
            }
            for (auto* leaf : leaves_) {
                if (leaf) { leaf->SetActive(false); }
            }
            for (auto* vine : vines_) {
                if (vine) { vine->SetActive(false); }
            }
        }
        return;
    }
    if (!board_->IsActive()) {
        board_->SetActive(true);
        for (auto* cap : { capLeft_, capRight_ }) {
            if (cap) { cap->SetActive(true); }
        }
        for (auto* leaf : leaves_) {
            if (leaf) { leaf->SetActive(true); }
        }
        for (auto* vine : vines_) {
            if (vine) { vine->SetActive(true); }
        }
    }

    // UI は停止中でも動かしたいので Unscaled を使う
    const float deltaTime = Time::UnscaledDeltaTime();
    const float time = Time::UnscaledTimeSinceStartup();

    if (gainPopActive_) {
        gainPopElapsed_ += deltaTime;
        if (gainPopElapsed_ >= std::max(0.05f, cvGainPopDuration.Get())) {
            gainPopActive_ = false;
        }
    }

    UpdateTargets();
    UpdateAnimation(deltaTime);
    ApplyLayout(time);
}

bool GameComponents::StaminaGaugeUIComponent::TryGetFillFrontTarget(
    Vector2& outPosition, Vector2& outSize) const
{
    if (!built_ || !cvEnabled.Get() || visiblePipCount_ == 0) {
        return false;
    }
    const std::size_t index = std::min(fillFrontIndex_, pips_.size() - 1);
    const UIImage* image = pips_[index].image;
    if (!image) {
        return false;
    }

    // 粒の軸は上端中央（縮んだときに下から食べられて見えるように置いてある）。
    // 飛んでくるバナナは粒の真ん中へ着けたいので、高さの半分だけ下げて返す。
    const Vector2 anchored = image->GetAnchoredPosition();
    outSize = image->GetSize();
    outPosition = { anchored.x, anchored.y + outSize.y * 0.5f };
    return true;
}

/// @note 上限を 2 まで許すのは、EaseOutBack を通した「行き過ぎ」をそのまま活かすため
void GameComponents::StaminaGaugeUIComponent::SetIntroReveal(float reveal)
{
    introReveal_ = std::clamp(reveal, 0.0f, 2.0f);
}

void GameComponents::StaminaGaugeUIComponent::PlayGainPop(float staminaAmount)
{
    if (!built_) {
        return;
    }
    gainPopElapsed_ = 0.0f;
    gainPopActive_ = true;

    // 入ってきた量ぶんの粒を、生え際から左へさかのぼって弾ませる。
    // 粒はスタミナが増えた時点で既に実っているので、ここでは伸び直させるだけ。
    // 「バナナが入った → この粒になった」を目で追えるようにするための演出。
    const float perPip = std::max(0.5f, cvStaminaPerPip.Get());
    const auto count = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::lround(staminaAmount / perPip)), 1, kGainPopMaxPips);

    for (std::size_t offset = 0; offset < count; ++offset) {
        if (fillFrontIndex_ < offset) {
            break;
        }
        Pip& pip = pips_[fillFrontIndex_ - offset];
        if (!pip.image || !pip.filled) {
            continue;
        }
        // grow を戻すと UpdateAnimation が GrowEase で伸び直す。行き過ぎがそのまま弾みになる
        pip.grow = std::min(pip.grow, kGainPopGrowFrom);
        pip.delay = static_cast<float>(offset) * kGrowStagger * 2.0f;
        pip.flash = 1.0f;
        pip.flashGain = true;
    }
}

float GameComponents::StaminaGaugeUIComponent::GainPopWave() const
{
    if (!gainPopActive_) {
        return 0.0f;
    }
    const float duration = std::max(0.05f, cvGainPopDuration.Get());
    const float progress = std::clamp(gainPopElapsed_ / duration, 0.0f, 1.0f);
    // 減衰する正弦波 1 周期。前半で持ち上がり、後半で沈んでから収まる
    return std::sin(progress * 2.0f * std::numbers::pi_v<float>) * (1.0f - progress);
}

void GameComponents::StaminaGaugeUIComponent::UpdateTargets()
{
    visiblePipCount_ = CalculatePipCount();

    const float perPip = std::max(0.5f, cvStaminaPerPip.Get());
    // 切り捨て。「あと何アクションぶん残っているか」と粒の数を一致させる
    const auto filledCount = static_cast<std::size_t>(
        std::max(0.0f, std::floor(hunger_->GetCurrentHunger() / perPip)));

    // 実っている一番右の粒。バナナが飛んでくる着地点になる。
    // 1 粒も実っていないときは「次に実る粒」＝先頭を指す
    fillFrontIndex_ = filledCount > 0
        ? std::min(filledCount, visiblePipCount_) - 1
        : 0;

    float stagger = 0.0f;
    for (std::size_t i = 0; i < pips_.size(); ++i) {
        Pip& pip = pips_[i];
        const bool filled = (i < filledCount) && (i < visiblePipCount_);
        if (filled != pip.filled) {
            pip.filled = filled;
            if (filled) {
                // 左から順に少しずつ実らせる
                pip.delay = stagger;
                stagger += kGrowStagger;
            } else {
                pip.delay = 0.0f;
            }
        }
    }

    // 次の 1 マスの予告。通常のレールより高いマス（橋・岩）のときだけ出す。
    // 毎回出すと常時点滅して煩いので、驚きが大きい場面に絞る
    previewPipCount_ = 0;
    if (builder_ && cvPreviewEnabled.Get()) {
        const float nextCost = builder_->GetNextPlacementCost();
        const float plainRailCost =
            hunger_->CalculateActionCost(GameSettings::RailStaminaCost.Get());
        if (nextCost > plainRailCost + 0.001f) {
            const auto pips = static_cast<std::size_t>(std::ceil(nextCost / perPip));
            previewPipCount_ = std::min(pips, filledCount);
        }
    }
    for (std::size_t i = 0; i < pips_.size(); ++i) {
        pips_[i].preview = (i < filledCount) && (filledCount - i <= previewPipCount_);
    }

    // 次の 1 マスすら払えないときは房ごと警告色で脈打たせる
    const float railCost = hunger_->CalculateActionCost(GameSettings::RailStaminaCost.Get());
    const bool low = hunger_->GetCurrentHunger() < railCost;
    lowPulse_ = MoveTowards(lowPulse_, low ? 1.0f : 0.0f, Time::UnscaledDeltaTime() * 4.0f);
}

void GameComponents::StaminaGaugeUIComponent::UpdateAnimation(float deltaTime)
{
    const float flashStrength = cvFlashStrength.Get();
    const float brightness = cvPipBrightness.Get();
    const float previewPhase = Time::UnscaledTimeSinceStartup() * 11.0f;

    for (Pip& pip : pips_) {
        if (!pip.image) {
            continue;
        }

        if (pip.delay > 0.0f) {
            pip.delay = std::max(0.0f, pip.delay - deltaTime);
        } else {
            const float target = pip.filled ? 1.0f : 0.0f;
            const float rate = pip.filled ? kGrowRate : kEatRate;
            pip.grow = MoveTowards(pip.grow, target, rate * deltaTime);
        }

        // 実が消えきったら皮へ、生え始めたら実へ差し替える。
        // テクスチャの差し替えはキャッシュヒットでもロックを取るので、変化した時だけ呼ぶ
        const bool showsFruit = pip.grow > 0.0f;
        if (showsFruit != pip.showsFruit) {
            pip.showsFruit = showsFruit;
            pip.image->SetTexture(showsFruit ? kTexPip : kTexPipEmpty);
            pip.flash = 1.0f;
            pip.flashGain = showsFruit;
        }

        pip.flash = std::max(0.0f, pip.flash - deltaTime * kFlashDecay);

        const float glow = brightness * (1.0f + pip.flash * flashStrength);
        const float warn = lowPulse_;
        // 増えた粒は緑、食べられた粒は赤へ一瞬振る。バナナの回復と消費が同時に
        // 起きても、どちらがいくつ動いたのかを色で切り分けられるようにする
        const float tintR = pip.flashGain ? (1.0f - pip.flash * 0.45f) : 1.0f;
        const float tintG = pip.flashGain ? 1.0f : (1.0f - pip.flash * 0.55f);
        const float tintB = 1.0f - pip.flash * (pip.flashGain ? 0.55f : 0.65f);
        // 予告は色ではなく明滅で示す。色は増減の意味に取ってあるので、
        // ここで色を足すと 3 つの意味が混ざって読めなくなる
        const float alpha = pip.preview
            ? 0.35f + 0.65f * (0.5f + 0.5f * std::sin(previewPhase))
            : 1.0f;
        pip.image->SetColor({
            glow * tintR,
            glow * tintG * (1.0f - warn * 0.35f),
            glow * tintB * (1.0f - warn * 0.45f),
            alpha });
    }
}

void GameComponents::StaminaGaugeUIComponent::ApplyLayout(float time)
{
    const float swaySpeed = cvSwaySpeed.Get();
    const float swayAmplitude = cvSwayAmplitude.Get();

    // 表示倍率。1 のときテクスチャと等倍になり、ドットが一番きれいに出る
    const float scale = std::max(0.1f, cvScale.Get());

    // バナナが入った反応。板ごと持ち上げるので、幅は変えずに全体が弾んで見える。
    // 横幅を変えると粒の並びが動いて、どの粒が増えたのか読めなくなる。
    const float gainPop = GainPopWave();
    const float gainStretch = 1.0f + gainPop * cvGainPopStretch.Get();
    const Vector2 basePosition = cvPosition.Get();
    const float pipW = kPipWidth * scale;
    const float pipH = kPipHeight * scale;
    const float pitch = kPipPitch * scale;
    const float bunchGap = kBunchGap * scale;
    const float capW = kCapWidth * scale;
    const float panelH = kPanelHeight * scale;
    const float padding = kInnerPadding * scale;
    const float swayBob = cvSwayBob.Get() * scale;

    // 粒の x 送りを先に求めて、板の幅を粒の数から決める
    float cursor = 0.0f;
    for (std::size_t i = 0; i < visiblePipCount_; ++i) {
        cursor += pitch;
        if ((i + 1) % kPipsPerBunch == 0 && i + 1 != visiblePipCount_) {
            cursor += bunchGap;
        }
    }
    const float contentWidth = std::max(pipW, cursor - (pitch - pipW));
    const float panelWidth = capW * 2.0f + padding * 2.0f + contentWidth;

    // 突入演出あけの登場。板の右端が画面外へ抜ける距離まで左へ寄せてから戻す。
    // 幅が粒の数で変わるので、寄せ幅もそのつど板の実寸から求める。
    const Vector2 origin{
        basePosition.x - (1.0f - introReveal_) * (basePosition.x + panelWidth + kIntroMargin),
        basePosition.y - gainPop * cvGainPopLift.Get() * scale };

    board_->SetAnchoredPosition({ origin.x + capW, origin.y });
    board_->SetSize({ panelWidth - capW * 2.0f, panelH });

    // 板と端木は残量が足りないときだけ赤へ寄せる
    const float warn = lowPulse_ * (0.5f + 0.5f * std::sin(time * 9.0f));
    const Vector4 boardColor{
        1.0f + warn * 0.25f,
        1.0f - warn * 0.35f,
        1.0f - warn * 0.40f,
        1.0f };
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

    // 蔦・葉も粒と同じくブルームで飛ぶので、板とは別に明るさを落とす
    const float foliage = cvFoliageBrightness.Get();
    const Vector4 foliageColor{ foliage, foliage, foliage, 1.0f };

    const float pipLeft = origin.x + capW + padding;
    const float pipTop = origin.y + kPipTopOffset * scale;

    float x = 0.0f;
    for (std::size_t i = 0; i < pips_.size(); ++i) {
        Pip& pip = pips_[i];
        if (!pip.image) {
            continue;
        }
        if (i >= visiblePipCount_) {
            if (pip.image->IsActive()) {
                pip.image->SetActive(false);
            }
            continue;
        }
        if (!pip.image->IsActive()) {
            pip.image->SetActive(true);
        }

        const float phase = static_cast<float>(i) * 0.55f;
        const float sway = std::sin(time * swaySpeed + phase) * swayAmplitude;
        const float bob = std::sin(time * swaySpeed * 0.8f + phase * 0.6f) * swayBob;

        // 実っているあいだだけ縦に伸縮させる。皮は常に原寸。
        // バナナが入った瞬間は、実っている粒だけまとめて縦へ伸ばす
        const float height = pip.showsFruit
            ? pipH * std::max(0.0f, GrowEase(pip.grow)) * gainStretch
            : pipH;

        pip.image->SetSize({ pipW, height });
        pip.image->SetAnchoredPosition({ pipLeft + x + pipW * 0.5f, pipTop + bob });
        pip.image->SetUIRotation(sway);

        x += pitch;
        if ((i + 1) % kPipsPerBunch == 0 && i + 1 != visiblePipCount_) {
            x += bunchGap;
        }
    }

    // 板の縁の蔦。上下交互に並べ、1 本ずつ位相をずらして揺らす
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
        vine->SetAnchoredPosition({
            origin.x + capW + inner * t,
            centerY + std::sin(time * swaySpeed * 0.5f + phase) * swayBob * 0.5f });
        vine->SetUIRotation(
            (onTop ? 0.0f : MathCore::Constants::kPi) +
            std::sin(time * swaySpeed * 0.45f + phase) * 0.05f);
        vine->SetColor(foliageColor);
    }

    // 端木に絡んだ葉。房より一段ゆっくり揺らして、風が抜けているように見せる
    if (leaves_.size() >= 2) {
        const float leafW = kLeafWidth * scale;
        const float leafH = kLeafHeight * scale;
        const float leafInset = 4.0f * kArtScale * scale;
        for (auto* leaf : leaves_) {
            leaf->SetSize({ leafW, leafH });
            leaf->SetColor(foliageColor);
        }
        leaves_[0]->SetAnchoredPosition({ origin.x + leafInset, origin.y + leafInset * 0.7f });
        leaves_[0]->SetUIRotation(-0.35f + std::sin(time * swaySpeed * 0.7f) * 0.12f);

        leaves_[1]->SetAnchoredPosition({
            origin.x + panelWidth - leafInset, origin.y + panelH - leafInset });
        leaves_[1]->SetUIRotation(
            MathCore::Constants::kPi + 0.35f + std::sin(time * swaySpeed * 0.7f + 1.7f) * 0.12f);
    }

}

#ifdef USE_IMGUI
bool GameComponents::StaminaGaugeUIComponent::DrawInspector()
{
    const bool changed = CVarUI::DrawTree("Game.StaminaGauge");
    UI::Hint("変更は CVars.json へ自動保存されます。");
    ImGui::Separator();
    ImGui::Text("粒: %zu / %zu", visiblePipCount_, kMaxPipCount);
    if (hunger_) {
        ImGui::TextDisabled(
            "スタミナ: %.1f / %.1f", hunger_->GetCurrentHunger(), hunger_->GetMaximumHunger());
    }
    return changed;
}
#endif
