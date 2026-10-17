using namespace CoreEngine;

// トロッコが画面の外へ見切れている間、画面の端にトロッコのアイコンと「あと○m」の板と向きの矢印を出す。
// はみ出しはトロッコの中心ではなく外形（半径）で測り、トロッコのいる距離での 1m の写り方から m に直す。
// 離れるほど数字が赤く、揺れが速くなる。ポーズ中も動かす（止めると案内だけが取り残される）
[DisplayName("画面外のトロッコの案内")]
class OffscreenTrainIndicator : ScriptComponent
{
    [DisplayName("案内を出す")]
    bool indicatorEnabled = true;

    [DisplayName("表示の倍率"), Range(0.3, 2.5)]
    float scale = 1.0f;

    [DisplayName("画面の端から留める幅（px・左右, 上下）")]
    Vector2 edgeMargin = Vector2(132.0f, 96.0f);

    [DisplayName("アイコンの明るさ（元の値）"), Range(0.02, 1.5)]
    float iconBrightness = 0.18f;

    [DisplayName("トロッコの半径（m）"), Range(0, 5)]
    float trolleyRadius = 1.0f;

    [DisplayName("出すまでの見切れ（m）"), Range(0, 20)]
    float showMeters = 0.5f;

    [DisplayName("現れる・消える時間（秒）"), Range(0, 1.5)]
    float fadeSeconds = 0.18f;

    [DisplayName("数字が赤へ振り切る距離（m）"), Range(1, 200)]
    float urgentMeters = 24.0f;

    [DisplayName("揺れの幅（px）"), Range(0, 40)]
    float bobPixels = 5.0f;

    [DisplayName("揺れの速さ（回/秒）"), Range(0, 12)]
    float bobSpeed = 2.6f;

    [DisplayName("描画順"), Range(0, 5000)]
    int sortOrder = 920;

    private TrainMovement@ train_;
    private GameManager@ gameManager_;
    private UIImage@ icon_;
    private UIImage@ board_;
    private UIImage@ capLeft_;
    private UIImage@ capRight_;
    private UIText@ prefix_;
    private UIText@ distance_;
    private UIText@ arrow_;
    private float elapsed_ = 0.0f;
    private float visibility_ = 0.0f;
    private bool wantsVisible_ = false;
    private int shownMeters_ = -1;
    private bool partsActive_ = true;

    // 測った結果
    private Vector2 canvasPosition_;
    private Vector2 outward_;
    private float metersToEdge_ = 0.0f;
    private bool offscreen_ = false;

    void Setup(TrainMovement@ train, GameManager@ gameManager)
    {
        @train_ = train;
        @gameManager_ = gameManager;
        @icon_ = owner.uiImage;
        if (!icon_.exists) {
            Error("OffscreenTrainIndicator: UI 画像に付けてください");
            @icon_ = null;
            return;
        }
        icon_.anchor = UIAnchor::TopLeft;
        icon_.pivot = Vector2(0.5f, 0.5f);
        icon_.sortOrder = sortOrder + 2;

        @board_ = SpawnHudImage(owner, "Application/Assets/Textures/UI/Board/board_mid.png", "TrainOffscreenBoard", sortOrder, Vector2(0, 0), Vector2(1, 1));
        @capLeft_ = SpawnHudImage(owner, kStaminaCapL, "TrainOffscreenBoardCapL", sortOrder + 1, Vector2(0, 0), Vector2(1, 1));
        @capRight_ = SpawnHudImage(owner, kStaminaCapR, "TrainOffscreenBoardCapR", sortOrder + 1, Vector2(0, 0), Vector2(1, 1));
        @prefix_ = SpawnLabel("851Gkktt_005.ttf", "あと", "TrainOffscreenPrefix", 20.0f, kOffscreenPrefixColor);
        @distance_ = SpawnLabel("x8y12pxDenkiChip.ttf", "0m", "TrainOffscreenDistance", 30.0f, kOffscreenNearColor);
        @arrow_ = SpawnLabel("x8y12pxDenkiChip.ttf", "◀", "TrainOffscreenArrow", 34.0f, kOffscreenNearColor);
        SetPartsActive(false);
    }

    void Update() override
    {
        if (icon_ is null || train_ is null) {
            return;
        }
        if (!indicatorEnabled || !Measure()) {
            wantsVisible_ = false;
            visibility_ = 0.0f;
            SetPartsActive(false);
            return;
        }
        const float deltaTime = Time::UnscaledDeltaTime();
        elapsed_ += deltaTime;
        UpdateVisibility(deltaTime);
        if (visibility_ <= 0.0f) {
            SetPartsActive(false);
            return;
        }
        SetPartsActive(true);
        // 切り上げる（「あと0m」は「もう見えている」と読めるので出さない）
        const int shown = Clamp(int(ceil(metersToEdge_)), 1, 9999);
        if (shown != shownMeters_) {
            shownMeters_ = shown;
            distance_.text = shown + "m";
        }
        LayoutParts();
    }

    private UIText@ SpawnLabel(const string &in font, const string &in text, const string &in name, float fontSize, const Vector4 &in color)
    {
        UIText@ label = SpawnHudText(owner, font, text, name, sortOrder + 3, fontSize);
        label.color = OriginalHudColor(color);
        label.SetOutline(OriginalHudColor(kOffscreenOutlineColor), 0.05f);
        return label;
    }

    // ---------------------------------------------------------------- 測る

    private bool Measure()
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (camera is null) {
            return false;
        }
        const Vector3 trainPosition = train_.GetWorldPosition();
        const Vector3 screen = camera.WorldToScreenPoint(trainPosition);
        if (screen.z <= 1.0e-5f) {
            // カメラの後ろ。横のずれをそのまま距離として扱う
            const Vector3 toTrain = trainPosition - camera.position;
            const float lateral = toTrain.x * camera.right.x + toTrain.y * camera.right.y + toTrain.z * camera.right.z;
            canvasPosition_ = Vector2(lateral >= 0.0f ? 1920.0f * 1.5f : 1920.0f * -0.5f, 540.0f);
            outward_ = Vector2(lateral >= 0.0f ? 1.0f : -1.0f, 0.0f);
            metersToEdge_ = abs(lateral);
            offscreen_ = true;
            return true;
        }

        // トロッコのいる距離での 1m の写り方を、カメラの右・上へ 1m ずらした点で測る
        const float ndcX = screen.x / 960.0f - 1.0f;
        const float ndcY = 1.0f - screen.y / 540.0f;
        float ndcPerMeterX = 0.0f;
        float ndcPerMeterY = 0.0f;
        const Vector3 probeX = camera.WorldToScreenPoint(trainPosition + camera.right);
        if (probeX.z > 1.0e-5f) {
            ndcPerMeterX = abs(probeX.x - screen.x) / 960.0f;
        }
        const Vector3 probeY = camera.WorldToScreenPoint(trainPosition + camera.up);
        if (probeY.z > 1.0e-5f) {
            ndcPerMeterY = abs(probeY.y - screen.y) / 540.0f;
        }

        const float radius = Max(trolleyRadius, 0.0f);
        const float overX = abs(ndcX) + ndcPerMeterX * radius - 1.0f;
        const float overY = abs(ndcY) + ndcPerMeterY * radius - 1.0f;
        float meters = 0.0f;
        if (overX > 0.0f && ndcPerMeterX > 0.0f) {
            meters = Max(meters, overX / ndcPerMeterX);
        }
        if (overY > 0.0f && ndcPerMeterY > 0.0f) {
            meters = Max(meters, overY / ndcPerMeterY);
        }
        canvasPosition_ = Vector2(screen.x, screen.y);
        // 矢印はより深く外れている軸へ向ける
        outward_ = overX >= overY ? Vector2(ndcX < 0.0f ? -1.0f : 1.0f, 0.0f) : Vector2(0.0f, ndcY > 0.0f ? -1.0f : 1.0f);
        metersToEdge_ = meters;
        offscreen_ = overX > 0.0f || overY > 0.0f;
        return true;
    }

    // 枠に触れてから閾値までの間は前の状態を保つ（境目で点滅しない）
    private void UpdateVisibility(float deltaTime)
    {
        const bool playing = gameManager_ is null || gameManager_.IsPlaying();
        if (!playing || !offscreen_) {
            wantsVisible_ = false;
        } else if (metersToEdge_ >= Max(showMeters, 0.0f)) {
            wantsVisible_ = true;
        }
        const float target = wantsVisible_ ? 1.0f : 0.0f;
        visibility_ = fadeSeconds > 0.0f ? MoveTowards(visibility_, target, deltaTime / fadeSeconds) : target;
    }

    // ---------------------------------------------------------------- 置く

    private void LayoutParts()
    {
        const float iconH = 104.0f * scale;
        const float iconW = iconH * 231.0f / 240.0f;
        const float panelH = 38.0f * 1.4f * scale;
        const float capW = 14.0f * 1.4f * scale;
        const float padding = 9.0f * 1.4f * scale;
        const float labelGap = 6.0f * 1.4f * scale;
        const float urgency = Clamp(metersToEdge_ / Max(urgentMeters, 0.01f), 0.0f, 1.0f);

        prefix_.fontSize = 20.0f * scale;
        distance_.fontSize = 30.0f * scale;
        const float prefixWidth = prefix_.measuredSize.x;
        const float distanceWidth = distance_.measuredSize.x;
        const float contentWidth = Max(92.0f * scale, prefixWidth + labelGap + distanceWidth);
        const float panelWidth = capW * 2.0f + padding * 2.0f + contentWidth;

        // 画面の端に留める。アイコンと板が枠から出ないぶん内側へ寄せる
        const float minX = Max(edgeMargin.x * scale, panelWidth * 0.5f + 4.0f);
        const float maxX = 1920.0f - minX;
        const float minY = Max(edgeMargin.y * scale, iconH * 0.5f + 4.0f);
        const float maxY = 1080.0f - Max(edgeMargin.y * scale, iconH * 0.5f + 8.0f * scale + panelH + 4.0f);
        const float bob = sin(elapsed_ * bobSpeed * (1.0f + urgency) * kTau) * bobPixels * scale;
        const float centerX = Clamp(canvasPosition_.x, minX, Max(minX, maxX));
        const float centerY = Clamp(canvasPosition_.y, minY, Max(minY, maxY)) + bob;

        // 出入りの間は画面の外から滑り込ませる
        const float slide = (1.0f - visibility_) * 26.0f * scale;
        const Vector2 iconCenter = Vector2(centerX + outward_.x * slide, centerY + outward_.y * slide);
        const float alpha = visibility_;
        const Vector4 number = OriginalHudColor(Lerp(kOffscreenNearColor, kOffscreenFarColor, urgency));
        const Vector4 numberColor = Vector4(number.x, number.y, number.z, alpha);
        const Vector4 outline = OriginalHudColor(kOffscreenOutlineColor);
        const Vector4 outlineColor = Vector4(outline.x, outline.y, outline.z, alpha);

        icon_.anchoredPosition = iconCenter;
        icon_.size = Vector2(iconW, iconH);
        const float iconGain = kOriginalHudExposure * iconBrightness;
        icon_.color = OriginalTextureTint(kTexMeanCart, Vector4(iconGain, iconGain, iconGain, alpha));

        // 矢印はアイコンの外側
        arrow_.anchoredPosition = Vector2(iconCenter.x + (iconW * 0.5f + 16.0f * scale) * outward_.x,
            iconCenter.y + (iconH * 0.5f + 16.0f * scale) * outward_.y);
        arrow_.fontSize = 34.0f * scale;
        arrow_.color = numberColor;
        arrow_.SetOutline(outlineColor, 0.05f);
        const string arrowText = outward_.x < 0.0f ? "◀" : outward_.x > 0.0f ? "▶" : outward_.y < 0.0f ? "▲" : "▼";
        if (arrow_.text != arrowText) {
            arrow_.text = arrowText;
        }

        // 板はアイコンの真下
        const float boardLeft = iconCenter.x - panelWidth * 0.5f;
        const float boardTop = iconCenter.y + iconH * 0.5f + 8.0f * scale;
        const Vector4 boardGain = Vector4(kOffscreenBoardColor.x, kOffscreenBoardColor.y, kOffscreenBoardColor.z, 0.0f) * kOriginalHudExposure;
        const Vector4 boardColor = OriginalTextureTint(kTexMeanBoardCap, Vector4(boardGain.x, boardGain.y, boardGain.z, alpha));
        board_.anchoredPosition = Vector2(boardLeft + capW, boardTop);
        board_.size = Vector2(panelWidth - capW * 2.0f, panelH);
        board_.color = OriginalTextureTint(kTexMeanBoard, Vector4(boardGain.x, boardGain.y, boardGain.z, alpha));
        capLeft_.anchoredPosition = Vector2(boardLeft, boardTop);
        capLeft_.size = Vector2(capW, panelH);
        capLeft_.color = boardColor;
        capRight_.anchoredPosition = Vector2(boardLeft + panelWidth - capW, boardTop);
        capRight_.size = Vector2(capW, panelH);
        capRight_.color = boardColor;

        // 「あと」と数字は板の中を左から詰める
        const float contentLeft = boardLeft + capW + padding;
        const float centerLineY = boardTop + panelH * 0.5f;
        const Vector4 prefixColor = OriginalHudColor(kOffscreenPrefixColor);
        prefix_.anchoredPosition = Vector2(contentLeft + prefixWidth * 0.5f, centerLineY);
        prefix_.color = Vector4(prefixColor.x, prefixColor.y, prefixColor.z, alpha);
        prefix_.SetOutline(outlineColor, 0.05f);
        distance_.anchoredPosition = Vector2(contentLeft + prefixWidth + labelGap + distanceWidth * 0.5f, centerLineY);
        distance_.color = numberColor;
        distance_.SetOutline(outlineColor, 0.05f);
    }

    // アイコン（自分）は消さずに透明にする（消すと Update が回らなくなる）
    private void SetPartsActive(bool active)
    {
        if (!active) {
            icon_.color = Vector4(1.0f, 1.0f, 1.0f, 0.0f);
        }
        if (partsActive_ == active) {
            return;
        }
        partsActive_ = active;
        board_.gameObject.active = active;
        capLeft_.gameObject.active = active;
        capRight_.gameObject.active = active;
        prefix_.gameObject.active = active;
        distance_.gameObject.active = active;
        arrow_.gameObject.active = active;
    }
}

// 元のゲームの HUD の値（OriginalHudColor で画面の色にする）
const Vector4 kOffscreenPrefixColor = Vector4(0.84f, 0.80f, 0.70f, 1.0f);
const Vector4 kOffscreenNearColor = Vector4(0.980f, 0.839f, 0.200f, 1.0f);
const Vector4 kOffscreenFarColor = Vector4(0.980f, 0.400f, 0.220f, 1.0f);
const Vector4 kOffscreenOutlineColor = Vector4(0.031f, 0.020f, 0.012f, 1.0f);
const Vector4 kOffscreenBoardColor = Vector4(0.42f, 0.50f, 0.30f, 1.0f);
