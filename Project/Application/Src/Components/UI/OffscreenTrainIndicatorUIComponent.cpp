#include "pch.h"
#include "OffscreenTrainIndicatorUIComponent.h"

#include "Components/GameCore/GameManagerComponent.h"
#include "Components/Train/TrainMovementComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector3.h"
#include "Text/FontManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

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
    // テクスチャ。アイコンはローディング画面のトロッコ、板はスタミナゲージの
    // 版下をそのまま使うので、新規アセットは無い
    // ───────────────────────────────────────────────────────────────
    constexpr const char* kTexIcon = "Application/Assets/Textures/loading_cart.png";
    constexpr const char* kTexBoardMid = "Application/Assets/Textures/Stamina/board_mid.png";
    constexpr const char* kTexBoardCapL = "Application/Assets/Textures/Stamina/board_cap_l.png";
    constexpr const char* kTexBoardCapR = "Application/Assets/Textures/Stamina/board_cap_r.png";

    // ───────────────────────────────────────────────────────────────
    // 版下の寸法（倍率 1.0 のときの px。基準解像度 1920x1080）
    // ───────────────────────────────────────────────────────────────
    /// loading_cart.png の実寸は 231x240。縦を決めて横をこの比で合わせる
    constexpr float kIconAspect = 231.0f / 240.0f;
    constexpr float kIconHeight = 104.0f;
    constexpr float kIconWidth = kIconHeight * kIconAspect;

    /// 板はスタミナゲージと同じ版下。38x14 が原寸 76x28 に当たる倍率で使う
    constexpr float kBoardArtScale = 1.4f;
    constexpr float kPanelHeight = 38.0f * kBoardArtScale;
    constexpr float kCapWidth = 14.0f * kBoardArtScale;
    constexpr float kInnerPadding = 9.0f * kBoardArtScale;
    /// 「あと」と数字のあいだ
    constexpr float kLabelGap = 6.0f * kBoardArtScale;

    constexpr float kPrefixFontSize = 20.0f;
    constexpr float kDistanceFontSize = 30.0f;
    constexpr float kArrowFontSize = 34.0f;

    /// アイコンの下端から板の上端まで
    constexpr float kIconBoardGap = 8.0f;
    /// アイコンの外側の縁から矢印の中心まで
    constexpr float kArrowGap = 16.0f;

    /// 板が空でも潰れないよう、中身の幅にはこれだけ下限を設ける
    constexpr float kMinContentWidth = 92.0f;

    // ───────────────────────────────────────────────────────────────
    // 色。スタミナゲージ・速度計と同じ系統に揃えてある
    // ───────────────────────────────────────────────────────────────
    const Vector4 kBoardColor{ 0.42f, 0.50f, 0.30f, 1.0f };      ///< 濁った緑寄りの木
    const Vector4 kPrefixColor{ 0.84f, 0.80f, 0.70f, 1.0f };     ///< 「あと」。数字より一段落とす
    const Vector4 kNearColor{ 0.980f, 0.839f, 0.200f, 1.0f };    ///< #FAD633（速度計の数字と同じ）
    const Vector4 kFarColor{ 0.980f, 0.400f, 0.220f, 1.0f };     ///< 離れるほどこちらへ寄せる
    const Vector4 kOutlineColor{ 0.031f, 0.020f, 0.012f, 1.0f }; ///< #080503
    constexpr float kOutlineWidth = 0.05f;

    // ───────────────────────────────────────────────────────────────
    // 調整用 CVar（CVars.json へ自動保存され、インスペクターから編集できる）
    // ───────────────────────────────────────────────────────────────
    CVar<bool> cvEnabled{
        "Game.TrainOffscreen.Enabled", true,
        "トロッコが画面外にいる間、画面の端にアイコンと残り距離を出す" };

    CVar<float> cvScale{
        "Game.TrainOffscreen.Scale", 1.0f,
        "案内全体の表示倍率。1.0 でアイコンの高さが 104px（基準解像度 1920x1080）",
        CVarRange{ 0.3f, 2.5f } };

    CVar<Vector2> cvEdgeMargin{
        "Game.TrainOffscreen.EdgeMargin", { 132.0f, 96.0f },
        "アイコンの中心を画面の端からこれだけ内側に留める [px]（左右, 上下）。"
        "左右は矢印がはみ出さない幅、上下はアイコンと板が収まる高さを見込んである",
        CVarRange{ 0.0f, 600.0f } };

    CVar<float> cvIconBrightness{
        "Game.TrainOffscreen.IconBrightness", 0.18f,
        "トロッコのアイコンの明るさ。UI は色補正前の HDR バッファへ描かれるので、"
        "露出（昼は約 4.5 倍）とブルームがそのまま乗る。"
        "loading_cart.png はトーンマップ後に合成されるローディング画面用の版下で、"
        "画面に出る明るさで描かれている（本体の灰色が linear 0.09、サルの顔が 0.54）。"
        "スタミナゲージ・速度計の版下は HDR 用に linear 0.002〜0.02 で描かれていて、"
        "そこへさらに 0.6〜0.7 を掛けている。等倍で貼ると 1 桁明るくなり白へ飛ぶ。"
        "既定の 0.18 は、本体が板の木（0.014）・顔が蔦の緑（0.116）と同じ範囲に来る値",
        CVarRange{ 0.02f, 1.5f } };

    CVar<float> cvTrolleyRadius{
        "Game.TrainOffscreen.TrolleyRadius", 1.0f,
        "トロッコの見た目の半径 [m]。既定はマス 1 つぶん（トロッコ本体＋乗っているサル）。"
        "この外形が画面の枠を越えた量で「はみ出し」を測る。"
        "中心だけで測ると、カメラが左右の中点を写す構図では中心が枠を越えることがまず無く、"
        "実際には大きく見切れているのに案内が出ない",
        CVarRange{ 0.0f, 5.0f } };

    CVar<float> cvShowMeters{
        "Game.TrainOffscreen.ShowMeters", 0.5f,
        "トロッコがこれだけ見切れたら案内を出す [m]。"
        "0 にすると枠に触れた瞬間から出るため、境目で点滅する。"
        "カメラがトロッコとカーソルの中点を写す都合で、トロッコが枠の外へ出られる余地は"
        "そもそも 1m 程度しかない。大きくすると出るべき場面でも出なくなる",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> cvFadeSeconds{
        "Game.TrainOffscreen.FadeSeconds", 0.18f,
        "案内が現れる／消えるまでの時間 [秒]",
        CVarRange{ 0.0f, 1.5f } };

    CVar<float> cvUrgentMeters{
        "Game.TrainOffscreen.UrgentMeters", 24.0f,
        "この距離まで離れたら数字が赤へ振り切る [m]。"
        "離れるほど赤くなり、揺れも速くなるので、数字を読まなくても遅れが分かる",
        CVarRange{ 1.0f, 200.0f } };

    CVar<float> cvBobPixels{
        "Game.TrainOffscreen.BobPixels", 5.0f,
        "アイコンが上下に揺れる幅 [px]。0 で揺れない",
        CVarRange{ 0.0f, 40.0f } };

    CVar<float> cvBobSpeed{
        "Game.TrainOffscreen.BobSpeed", 2.6f,
        "揺れのはやさ [回/秒]。離れるほどこの 2 倍まで速くなる",
        CVarRange{ 0.0f, 12.0f } };

    CVar<int> cvSortOrder{
        "Game.TrainOffscreen.SortOrder", 920,
        "案内の描画順（大きいほど手前）。スタミナゲージ(900)より手前、"
        "開始案内(1000)とポーズメニュー(2000)より奥",
        CVarRange{ 0.0f, 5000.0f } };

    /// @brief UIImage を 1 枚生やす
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

    /// @brief 文字を 1 枚生やす
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

    /// @brief ワールド座標を NDC へ落とす
    /// @return カメラの後ろにある点は測れないので false
    /// @note TAA のジッタは射影行列へ NDC 単位で足してあるので、ここで抜かないと
    ///       UI だけが 1px 未満で震える（BananaHarvestEffect の WorldToCanvas と同じ扱い）。
    bool ProjectToNdc(const Camera& camera, const Matrix4x4& viewProjection,
        const Vector3& world, Vector2& outNdc)
    {
        const Vector4 clip = MathCore::CoordinateTransform::TransformCoord(
            Vector4{ world.x, world.y, world.z, 1.0f }, viewProjection);
        // w <= 0 はカメラの後ろ。割ると符号が反転して、背後の点が画面内へ出てしまう
        if (clip.w <= 1.0e-5f) {
            return false;
        }
        outNdc = {
            clip.x / clip.w - camera.GetProjectionJitterX(),
            clip.y / clip.w - camera.GetProjectionJitterY() };
        return true;
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

void GameComponents::OffscreenTrainIndicatorUIComponent::Awake()
{
    if (!train_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "OffscreenTrainIndicatorUIComponent: TrainMovement が未設定です");
        SetEnabled(false);
        return;
    }
    BuildParts();
}

void GameComponents::OffscreenTrainIndicatorUIComponent::BuildParts()
{
    auto* owner = GetOwner();
    if (!owner) {
        return;
    }

    // オーナー自身がトロッコのアイコン
    icon_ = dynamic_cast<UIImage*>(owner);
    if (!icon_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "OffscreenTrainIndicatorUIComponent: UIImage にアタッチしてください");
        SetEnabled(false);
        return;
    }

    const int baseOrder = cvSortOrder.Get();
    icon_->SetAnchor(UIAnchor::TopLeft);
    icon_->SetPivot({ 0.5f, 0.5f });
    icon_->SetSortOrder(baseOrder + 2);

    // 板は中板＋端木の 3 枚。伸ばすのは横方向に一様な中板だけに限る
    board_ = SpawnPart(owner, kTexBoardMid, "TrainOffscreenBoard", baseOrder);
    boardCapLeft_ = SpawnPart(owner, kTexBoardCapL, "TrainOffscreenBoardCapL", baseOrder + 1);
    boardCapRight_ = SpawnPart(owner, kTexBoardCapR, "TrainOffscreenBoardCapR", baseOrder + 1);
    for (auto* part : { board_, boardCapLeft_, boardCapRight_ }) {
        if (part) {
            part->SetPivot({ 0.0f, 0.0f });
        }
    }

    auto* engine = owner->GetEngineSystem();
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    if (!fontManager) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "OffscreenTrainIndicatorUIComponent: FontManager が無いため文字を出しません");
        built_ = true;
        return;
    }

    // 数字は地面の 5m 目盛り（MapViewComponent）と同じドット絵フォントで打つ。
    // 目盛りと同じ字面なので、距離の単位が同じものだと読み替えなくても分かる
    MsdfFontDesc distanceFontDesc;
    distanceFontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    distanceFontDesc.systemFamilyNames = { L"Segoe UI" };
    distanceFontDesc.charsetUtf8 = "0123456789m";
    auto* distanceFont = fontManager->Acquire(distanceFontDesc);

    // 和文はスタミナゲージ・速度計の見出しと同じフォント
    MsdfFontDesc prefixFontDesc;
    prefixFontDesc.filePath = L"Engine/Assets/font/851Gkktt_005.ttf";
    prefixFontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"Segoe UI" };
    prefixFontDesc.charsetUtf8 = "あと";
    auto* prefixFont = fontManager->Acquire(prefixFontDesc);

    // 三角はドット絵フォントに無いのでシステムフォントへ落ちる
    //（速度計の「▼」と同じ組み合わせ）
    MsdfFontDesc arrowFontDesc;
    arrowFontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
    arrowFontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"MS Gothic" };
    arrowFontDesc.charsetUtf8 = "◀▶▲▼";
    auto* arrowFont = fontManager->Acquire(arrowFontDesc);

    if (prefixFont) {
        prefixLabel_ = SpawnLabel(
            owner, prefixFont, "あと", "TrainOffscreenPrefix",
            kPrefixFontSize, kPrefixColor, baseOrder + 3);
    }
    if (distanceFont) {
        distanceLabel_ = SpawnLabel(
            owner, distanceFont, "0m", "TrainOffscreenDistance",
            kDistanceFontSize, kNearColor, baseOrder + 3);
    }
    if (arrowFont) {
        arrow_ = SpawnLabel(
            owner, arrowFont, "◀", "TrainOffscreenArrow",
            kArrowFontSize, kNearColor, baseOrder + 3);
    }

    // 出るのは画面外にいる間だけ。最初のフレームから見えないよう畳んでおく
    SetPartsActive(false);
    built_ = true;
}

bool GameComponents::OffscreenTrainIndicatorUIComponent::Measure(Measurement& out) const
{
    if (!train_ || !viewCamera_) {
        return false;
    }

    const Vector3 trainPosition = train_->GetWorldPosition();
    const Matrix4x4 viewProjection =
        viewCamera_->GetViewMatrix() * viewCamera_->GetProjectionMatrix();

    Vector2 ndc{};
    if (!ProjectToNdc(*viewCamera_, viewProjection, trainPosition, ndc)) {
        // カメラの後ろ。ゲームのリグ（真後ろ上空から見下ろす）では起こらないが、
        // 起きたときに嘘の数字を出さないよう、横のずれをそのまま距離として扱う
        const Vector3 toTrain = trainPosition - viewCamera_->GetTranslate();
        const float lateral = Dot(toTrain, viewCamera_->GetRight());
        out.canvasPosition = {
            lateral >= 0.0f ? static_cast<float>(WinApp::kReferenceWidth) * 1.5f
                            : static_cast<float>(WinApp::kReferenceWidth) * -0.5f,
            static_cast<float>(WinApp::kReferenceHeight) * 0.5f };
        out.outward = { lateral >= 0.0f ? 1.0f : -1.0f, 0.0f };
        out.metersToEdge = std::abs(lateral);
        out.offscreen = true;
        return true;
    }

    // 画面 1 NDC あたり何メートルかを、トロッコがいるその場で測る。
    // 同じ長さでも遠ければ小さく写るので、割合を決め打ちにせず、カメラの右・上へ
    // 1m 動かした点を射影して画面上でどれだけ離れるかを見る
    //（RailBuilderComponent::IsInsideScreen がカーソルの半径を測るのと同じやり方）。
    constexpr float kProbeMeters = 1.0f;
    float ndcPerMeterX = 0.0f;
    float ndcPerMeterY = 0.0f;
    Vector2 probe{};
    if (ProjectToNdc(*viewCamera_, viewProjection,
        trainPosition + viewCamera_->GetRight() * kProbeMeters, probe)) {
        ndcPerMeterX = std::abs(probe.x - ndc.x) / kProbeMeters;
    }
    if (ProjectToNdc(*viewCamera_, viewProjection,
        trainPosition + viewCamera_->GetUp() * kProbeMeters, probe)) {
        ndcPerMeterY = std::abs(probe.y - ndc.y) / kProbeMeters;
    }

    // 射影行列のアスペクト比は基準解像度に固定されている（Camera::ResolveAspectRatio）。
    // NDC の ±1 がそのままレターボックス後の表示領域の端になる。
    // 測るのは中心ではなくトロッコの外形。カメラはトロッコとカーソルの中点を写すので、
    // カーソルを端で止めている限りトロッコの中心はほぼ枠の内側に残る。それでも画面の
    // 端では大きく見切れているので、外形で測らないと「見えていないのに出ない」になる
    const float radius = std::max(cvTrolleyRadius.Get(), 0.0f);
    const float overX = std::abs(ndc.x) + ndcPerMeterX * radius - 1.0f;
    const float overY = std::abs(ndc.y) + ndcPerMeterY * radius - 1.0f;

    float meters = 0.0f;
    if (overX > 0.0f && ndcPerMeterX > 0.0f) {
        meters = std::max(meters, overX / ndcPerMeterX);
    }
    if (overY > 0.0f && ndcPerMeterY > 0.0f) {
        meters = std::max(meters, overY / ndcPerMeterY);
    }

    out.canvasPosition = {
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(WinApp::kReferenceWidth),
        // NDC の Y は上が正、画面座標は下が正なので反転する
        (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(WinApp::kReferenceHeight) };
    // 矢印は「より深く外れている軸」へ向ける。斜めに外れても迷わないようにする
    out.outward = (overX >= overY)
        ? Vector2{ ndc.x < 0.0f ? -1.0f : 1.0f, 0.0f }
        : Vector2{ 0.0f, ndc.y > 0.0f ? -1.0f : 1.0f };
    out.metersToEdge = meters;
    out.offscreen = (overX > 0.0f || overY > 0.0f);
    return true;
}

void GameComponents::OffscreenTrainIndicatorUIComponent::UpdateVisibility(
    const Measurement& measurement, float deltaTime)
{
    // 終了演出中はカメラがトロッコへ寄っていく。案内はもう用が無いので畳む
    const bool playing =
        !gameManager_ || gameManager_->GetPhase() == GameManagerComponent::Phase::Playing;

    if (!playing) {
        wantsVisible_ = false;
    }
    else if (!measurement.offscreen) {
        wantsVisible_ = false;
    }
    else if (measurement.metersToEdge >= std::max(cvShowMeters.Get(), 0.0f)) {
        wantsVisible_ = true;
    }
    // 端に触れてから閾値までのあいだは前の状態を保つ。
    // ここで切り替えると、境目にいるトロッコで案内が点滅する

    const float fadeSeconds = std::max(cvFadeSeconds.Get(), 0.0f);
    const float target = wantsVisible_ ? 1.0f : 0.0f;
    visibility_ = (fadeSeconds > 0.0f)
        ? MoveTowards(visibility_, target, deltaTime / fadeSeconds)
        : target;
}

void GameComponents::OffscreenTrainIndicatorUIComponent::UpdateDistanceLabel(float meters)
{
    if (!distanceLabel_) {
        return;
    }

    // 切り上げる。1m を切っていても「あと0m」は出さない
    //（0 は「もう見えている」の意味になってしまう）
    const int shown = std::clamp(static_cast<int>(std::ceil(meters)), 1, 9999);
    if (shown == shownMeters_) {
        return;
    }
    shownMeters_ = shown;
    // 幅は LayoutParts が倍率を掛けたあとで測り直す
    distanceLabel_->SetText(std::to_string(shown) + "m");
}

void GameComponents::OffscreenTrainIndicatorUIComponent::LayoutParts(
    const Measurement& measurement, float time)
{
    const float scale = cvScale.Get();
    const float iconW = kIconWidth * scale;
    const float iconH = kIconHeight * scale;
    const float panelH = kPanelHeight * scale;
    const float capW = kCapWidth * scale;
    const float padding = kInnerPadding * scale;
    const float labelGap = kLabelGap * scale;

    // 離れているほど 1 に近づく。数字の色と揺れの速さに掛かる
    const float urgency = std::clamp(
        measurement.metersToEdge / std::max(cvUrgentMeters.Get(), 0.01f), 0.0f, 1.0f);

    // 幅を測る前に倍率を反映させる。測ってから変えると板の幅が 1 フレーム遅れる
    if (prefixLabel_) {
        prefixLabel_->SetFontSize(kPrefixFontSize * scale);
        shownPrefixWidth_ = prefixLabel_->GetMeasuredSize().x;
    }
    if (distanceLabel_) {
        distanceLabel_->SetFontSize(kDistanceFontSize * scale);
        shownDistanceWidth_ = distanceLabel_->GetMeasuredSize().x;
    }

    // 板の幅は実測した文字幅から決める。桁が増えても字が端木に触れない
    const float contentWidth = std::max(
        kMinContentWidth * scale,
        shownPrefixWidth_ + labelGap + shownDistanceWidth_);
    const float panelWidth = capW * 2.0f + padding * 2.0f + contentWidth;

    // 画面の端に留める。アイコンと板が枠から出ないぶんだけ内側へ寄せる
    const Vector2 margin = cvEdgeMargin.Get();
    const float minX = std::max(margin.x * scale, panelWidth * 0.5f + 4.0f);
    const float maxX = static_cast<float>(WinApp::kReferenceWidth) - minX;
    const float minY = std::max(margin.y * scale, iconH * 0.5f + 4.0f);
    const float maxY = static_cast<float>(WinApp::kReferenceHeight)
        - std::max(margin.y * scale, iconH * 0.5f + kIconBoardGap * scale + panelH + 4.0f);

    // 揺れは上下だけ。左右に振ると端に留めている意味が薄れる
    const float bobSpeed = cvBobSpeed.Get() * (1.0f + urgency);
    const float bob = std::sin(time * bobSpeed * MathCore::Constants::kPi * 2.0f)
        * cvBobPixels.Get() * scale;

    const float centerX = std::clamp(measurement.canvasPosition.x, minX, std::max(minX, maxX));
    const float centerY = std::clamp(measurement.canvasPosition.y, minY, std::max(minY, maxY)) + bob;

    // 出入りのあいだは、画面の外から滑り込ませる
    const float slide = (1.0f - visibility_) * 26.0f * scale;
    const Vector2 iconCenter{
        centerX + measurement.outward.x * slide,
        centerY + measurement.outward.y * slide };

    const float alpha = visibility_;
    const Vector4 numberColor{
        std::lerp(kNearColor.x, kFarColor.x, urgency),
        std::lerp(kNearColor.y, kFarColor.y, urgency),
        std::lerp(kNearColor.z, kFarColor.z, urgency),
        alpha };

    icon_->SetAnchoredPosition(iconCenter);
    icon_->SetSize({ iconW, iconH });
    const float iconBrightness = cvIconBrightness.Get();
    icon_->SetColor({ iconBrightness, iconBrightness, iconBrightness, alpha });

    if (arrow_) {
        // アイコンの外側へ置く。上下に外れているときは矢印もアイコンの上下へ回す
        const float offsetX = (iconW * 0.5f + kArrowGap * scale) * measurement.outward.x;
        const float offsetY = (iconH * 0.5f + kArrowGap * scale) * measurement.outward.y;
        arrow_->SetAnchoredPosition({ iconCenter.x + offsetX, iconCenter.y + offsetY });
        arrow_->SetFontSize(kArrowFontSize * scale);
        arrow_->SetColor(numberColor);
        arrow_->SetOutline({ kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, alpha },
            kOutlineWidth);
        arrow_->SetText(
            (measurement.outward.x < 0.0f) ? "◀"
            : (measurement.outward.x > 0.0f) ? "▶"
            : (measurement.outward.y < 0.0f) ? "▲" : "▼");
    }

    // 板はアイコンの真下。中央を揃える
    const float boardLeft = iconCenter.x - panelWidth * 0.5f;
    const float boardTop = iconCenter.y + iconH * 0.5f + kIconBoardGap * scale;
    const Vector4 boardColor{ kBoardColor.x, kBoardColor.y, kBoardColor.z, alpha };

    if (board_) {
        board_->SetAnchoredPosition({ boardLeft + capW, boardTop });
        board_->SetSize({ panelWidth - capW * 2.0f, panelH });
        board_->SetColor(boardColor);
    }
    if (boardCapLeft_) {
        boardCapLeft_->SetAnchoredPosition({ boardLeft, boardTop });
        boardCapLeft_->SetSize({ capW, panelH });
        boardCapLeft_->SetColor(boardColor);
    }
    if (boardCapRight_) {
        boardCapRight_->SetAnchoredPosition({ boardLeft + panelWidth - capW, boardTop });
        boardCapRight_->SetSize({ capW, panelH });
        boardCapRight_->SetColor(boardColor);
    }

    // 「あと」と数字は板の中身を左から順に詰める
    const float contentLeft = boardLeft + capW + padding;
    const float centerLineY = boardTop + panelH * 0.5f;
    if (prefixLabel_) {
        prefixLabel_->SetAnchoredPosition(
            { contentLeft + shownPrefixWidth_ * 0.5f, centerLineY });
        prefixLabel_->SetColor({ kPrefixColor.x, kPrefixColor.y, kPrefixColor.z, alpha });
        prefixLabel_->SetOutline(
            { kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, alpha }, kOutlineWidth);
    }
    if (distanceLabel_) {
        distanceLabel_->SetAnchoredPosition(
            { contentLeft + shownPrefixWidth_ + labelGap + shownDistanceWidth_ * 0.5f,
              centerLineY });
        distanceLabel_->SetColor(numberColor);
        distanceLabel_->SetOutline(
            { kOutlineColor.x, kOutlineColor.y, kOutlineColor.z, alpha }, kOutlineWidth);
    }
}

void GameComponents::OffscreenTrainIndicatorUIComponent::SetPartsActive(bool active)
{
    // オーナー（アイコン）だけは非アクティブにしない。GameObjectManager は
    // 非アクティブなオブジェクトの更新を丸ごと飛ばすので、消したが最後
    // このコンポーネントの Update() が二度と回らず、案内が戻らなくなる。
    // アイコンは透明にして畳む
    if (icon_ && !active) {
        icon_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
    for (auto* part : { board_, boardCapLeft_, boardCapRight_ }) {
        if (part && part->IsActive() != active) {
            part->SetActive(active);
        }
    }
    for (auto* text : { arrow_, prefixLabel_, distanceLabel_ }) {
        if (text && text->IsActive() != active) {
            text->SetActive(active);
        }
    }
}

void GameComponents::OffscreenTrainIndicatorUIComponent::Update()
{
    if (!built_ || !icon_) {
        return;
    }

    if (!cvEnabled.Get()) {
        wantsVisible_ = false;
        visibility_ = 0.0f;
        SetPartsActive(false);
        return;
    }

    Measurement measurement{};
    if (!Measure(measurement)) {
        wantsVisible_ = false;
        visibility_ = 0.0f;
        SetPartsActive(false);
        return;
    }

    // ポーズ中も動かす。止めると案内だけが取り残されて見える
    const float deltaTime = Time::UnscaledDeltaTime();
    elapsed_ += deltaTime;
    UpdateVisibility(measurement, deltaTime);

    // 消えきっているあいだは描画も配置もしない
    if (visibility_ <= 0.0f) {
        SetPartsActive(false);
        return;
    }

    SetPartsActive(true);
    UpdateDistanceLabel(measurement.metersToEdge);
    LayoutParts(measurement, elapsed_);
}

#ifdef USE_IMGUI
bool GameComponents::OffscreenTrainIndicatorUIComponent::DrawInspector()
{
    const bool changed = CVarUI::DrawTree("Game.TrainOffscreen");
    UI::Hint("変更は CVars.json へ自動保存されます。");
    ImGui::Separator();

    Measurement measurement{};
    if (Measure(measurement)) {
        ImGui::Text("画面外: %s", measurement.offscreen ? "はい" : "いいえ");
        ImGui::Text("端まで: %.1f m", measurement.metersToEdge);
        ImGui::TextDisabled(
            "投影先: (%.0f, %.0f) px",
            measurement.canvasPosition.x, measurement.canvasPosition.y);
    }
    else {
        ImGui::TextDisabled("カメラかトロッコが未設定です");
    }
    ImGui::TextDisabled("表示: %.2f", visibility_);
    return changed;
}
#endif
