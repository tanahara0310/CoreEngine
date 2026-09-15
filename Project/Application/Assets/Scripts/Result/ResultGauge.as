const string kGaugeTexPlankMid = "Application/Assets/Textures/Pause/plank_mid.png";
const string kGaugeTexPlankCapL = "Application/Assets/Textures/Pause/plank_cap_l.png";
const string kGaugeTexPlankCapR = "Application/Assets/Textures/Pause/plank_cap_r.png";
const string kGaugeTexVineV = "Application/Assets/Textures/Pause/vine_v.png";
const string kGaugeTexVineH = "Application/Assets/Textures/Pause/vine_h.png";
const string kGaugeTexFoliage = "Application/Assets/Textures/Pause/foliage.png";
const string kGaugeTexCursor = "Application/Assets/Textures/Pause/cursor.png";
const string kGaugeTexLeafM = "Application/Assets/Textures/Pause/leaf_m.png";
const string kGaugeTexLeafS = "Application/Assets/Textures/Pause/leaf_s.png";
const string kGaugeTexDim = "Application/Assets/Textures/Pause/dim.png";
const string kGaugeTexCart = "Application/Assets/Textures/result_cart.png";

const string kGaugeSeTick = "Application/Assets/Sounds/SE/rail_build.mp3";
const string kGaugeSeArrive = "Application/Assets/Sounds/SE/title_bound.mp3";
const string kGaugeSePassRecord = "Application/Assets/Sounds/SE/build.mp3";

// ドット絵のフォントと、起動時に焼いておく文字
const string kGaugePixelFontFile = "Engine/Assets/font/x8y12pxDenkiChip.ttf";
const array<string> kGaugePixelFontFamilies = { "Yu Gothic UI", "Meiryo", "Segoe UI" };
const string kGaugePixelCharset = "つれてきたサルひき" + "もくひょうまであとｍ" + "とっぱ！＋" + "まえのきろく" + "さいこうきろく"
    + "しんきろく" + "しょうごう" + "はじめのいっぽ" + "みならいレールこう" + "かけだしのつなぎて" + "ジャングルのあんないやく"
    + "トロッコのたつじん" + "でんせつのサル" + "もういちど" + "タイトルへ" + "えらぶけってい" + "← →";

// 絵の大きさ（px）
const float kGaugePlankHeight = 96.0f;
const float kGaugeCapWidth = 40.0f;
const float kGaugeVineWidth = 32.0f;
const float kGaugeCreeperWidth = 128.0f;
const float kGaugeCreeperHeight = 48.0f;
const float kGaugeFoliageWidth = 208.0f;
const float kGaugeFoliageHeight = 128.0f;
const float kGaugeCursorWidth = 128.0f;
const float kGaugeCursorHeight = 64.0f;
const float kGaugeLeafWidth = 48.0f;
const float kGaugeLeafHeight = 28.0f;

// 配置（基準解像度 1920x1080。アンカーは上端の中央で、x は画面の中央から、y は上端から測る）
const float kGaugeHeadTop = 44.0f;
const float kGaugeHeadWidth = 780.0f;
const float kGaugeHeadRow1Y = kGaugeHeadTop + kGaugePlankHeight * 0.5f;
const float kGaugeHeadRow2Y = kGaugeHeadTop + kGaugePlankHeight * 1.5f;
const float kGaugeDistanceFontSize = 84.0f;
const float kGaugeRemainFontSize = 32.0f;
const array<float> kGaugeHeadVineX = { -290.0f, -155.0f, 0.0f, 155.0f, 290.0f };

const float kGaugeSideCenterX = -644.0f;
const float kGaugeSideWidth = 452.0f;
const float kGaugeSideY = 92.0f;
const float kGaugeSideFontSize = 30.0f;
const float kGaugeRankY = 176.0f;
const float kGaugeRankHeight = 76.0f;
const float kGaugeRankFontSize = 26.0f;
const float kGaugeBestCenterX = 644.0f;
const float kGaugeBestY = kGaugeSideY;
const float kGaugeBestWidth = kGaugeSideWidth;
const float kGaugeBestHeight = kGaugePlankHeight;
const float kGaugeBestFontSize = kGaugeSideFontSize;

const float kGaugeNewRecordX = 470.0f;
const float kGaugeNewRecordY = 206.0f;
const float kGaugeNewRecordWidth = 300.0f;
const float kGaugeNewRecordRotation = -0.13f;
const float kGaugeNewRecordFontSize = 40.0f;

const float kGaugeRailY = 800.0f;
const float kGaugeRailLeft = -760.0f;
const float kGaugeGoalX = 600.0f;
const float kGaugeRailHeight = 44.0f;
const float kGaugeTickStepMeters = 100.0f;
const int kGaugeTiesPerTick = 5;
const float kGaugeMinTiePitch = 24.0f;
const int kGaugeMaxTicks = 16;
const float kGaugeSleeperWidth = 15.0f;
const float kGaugeSleeperHeight = 34.0f;
const float kGaugeRailVineStep = 96.0f;
const float kGaugeTickFontSize = 24.0f;
const float kGaugeTickLabelY = kGaugeRailY + 58.0f;
const float kGaugeTickPostWidth = kGaugeSleeperWidth;
const float kGaugeTickPostHeight = 52.0f;
const float kGaugeTickPostY = kGaugeRailY - kGaugeSleeperHeight * 0.5f + kGaugeTickPostHeight * 0.5f;
const float kGaugeGateBeamY = 638.0f;
const float kGaugeGateBeamWidth = 350.0f;
const float kGaugeGatePostWidth = 40.0f;
const float kGaugeGatePostTop = kGaugeGateBeamY + kGaugePlankHeight * 0.5f;
const float kGaugeGateFontSize = 30.0f;
const float kGaugeCartDot = 5.0f;
const float kGaugeCartWidth = kGaugeCartDot * 25.0f;
const float kGaugeCartHeight = kGaugeCartDot * 26.0f;
const float kGaugeCartY = kGaugeRailY - kGaugeRailHeight * 0.5f - kGaugeCartHeight * 0.5f + 6.0f + kGaugeCartDot;
const float kGaugeOverGoalMaxX = 46.0f;

const float kGaugeRecordPlankWidth = 300.0f;
const float kGaugeRecordPlankHeight = 52.0f;
const float kGaugeRecordPlankY = kGaugeRailY - kGaugeRailHeight * 0.5f - kGaugeCartHeight + 6.0f + kGaugeCartDot
    - kGaugeRecordPlankHeight * 0.5f - 8.0f;
const float kGaugeRecordPostTop = kGaugeRecordPlankY + kGaugeRecordPlankHeight * 0.5f;
const float kGaugeRecordPostBottom = 806.0f;
const float kGaugeRecordPostWidth = 20.0f;
const float kGaugeRecordFontSize = 24.0f;

const float kGaugeChoiceY = 940.0f;
const float kGaugeChoiceOffsetX = 262.0f;
const float kGaugeChoiceWidth = 420.0f;
const float kGaugeChoiceFontSize = 44.0f;
const float kGaugeChoiceTilt = -0.030f;

const float kGaugeCanvasWidth = 1920.0f;
const float kGaugeTipY = 1042.0f;
const float kGaugeTipFontSize = 34.0f;
const float kGaugeTipBgHeight = 64.0f;
const float kGaugeTipBgAlpha = 0.5f;
const float kGaugeTipPulseSeconds = 1.6f;
const float kGaugeTipPulseMin = 0.30f;

// 演出
const float kGaugeOutlineWidth = 0.05f;
const float kGaugeSwaySpeed = 1.4f;
const float kGaugeSwayAngle = 0.012f;
const float kGaugeCursorFollow = 22.0f;
const float kGaugeCursorGap = 34.0f;
const float kGaugeLeafGravity = 900.0f;
const float kGaugeLeafFadeSeconds = 0.35f;
const float kGaugeMaxStepSeconds = 0.1f;
const float kGaugeExposureScaleMin = 0.02f;
const float kGaugeExposureScaleMax = 4.0f;
const float kGaugeSeTickVolume = 0.35f;
const float kGaugeSeArriveVolume = 0.7f;
const uint kGaugeLeafCount = 18;

// 目標の距離に対する割合から称号を決める
string ResultGaugeRankName(float ratio)
{
    if (ratio >= 1.4f) {
        return "でんせつのサル";
    }
    if (ratio >= 1.0f) {
        return "トロッコのたつじん";
    }
    if (ratio >= 0.8f) {
        return "あと いっぽ";
    }
    if (ratio >= 0.6f) {
        return "ジャングルのあんないやく";
    }
    if (ratio >= 0.4f) {
        return "かけだしのつなぎて";
    }
    if (ratio >= 0.2f) {
        return "みならいレールこう";
    }
    return "はじめのいっぽ";
}

// offset を原点の周りに angle だけ回す
Vector2 ResultGaugeRotateOffset(const Vector2 &in offset, float angle)
{
    const float c = cos(angle);
    const float s = sin(angle);
    return Vector2(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
}

// current を target へ、rate の速さで指数的に寄せる
float ResultGaugeFollow(float current, float target, float rate, float deltaTime)
{
    return current + (target - current) * (1.0f - exp(-rate * deltaTime));
}

// 端木・中板・端木の 3 枚組の板
class ResultPlank
{
    UIImage@ mid;
    UIImage@ capLeft;
    UIImage@ capRight;
}

// 選択肢の木札 1 枚。板とツタは文字の位置と大きさに合わせて動く
class ResultChoice
{
    ResultPlank@ plank;
    array<UIImage@> creepers = array<UIImage@>(3);
    UIText@ text;
    float baseFontSize = 44.0f;
    float baseY = 0.0f;
    ResultButtonAnimator@ animator;
}

// 舞う葉 1 枚
class ResultLeaf
{
    UIImage@ image;
    Vector2 position;
    Vector2 velocity;
    float rotation = 0.0f;
    float spin = 0.0f;
    float life = 0.0f;
    float lifeSpan = 1.0f;
    float size = 1.0f;
}

// リザルトの UI。今回の距離を記録へ残し、目標までのゲージ・前回の杭・自己最高・称号・選択肢・Tips を作って、
// 距離を 0 から数えあげながらトロッコをレールの上で走らせる。
// UI はトーンマップ前に描かれるので、色には自動露出の分を毎フレーム打ち消した値を入れる
[DisplayName("リザルトのゲージ")]
class ResultGauge : ScriptComponent
{
    [DisplayName("目標の距離（m）"), Range(50, 5000)]
    float goalMeters = 500.0f;

    [DisplayName("0m から数えあげる時間（秒）"), Range(0, 5)]
    float countSeconds = 1.15f;

    [DisplayName("板とツタの明るさ"), Range(0.2, 2)]
    float brightness = 1.0f;

    [DisplayName("トロッコの明るさ"), Range(0.05, 1.5)]
    float railBrightness = 0.32f;

    [DisplayName("描画順（大きいほど手前）"), Range(0, 8000)]
    int sortOrder = 1500;

    [DisplayName("前回の記録の杭を出す")]
    bool showPreviousRecord = true;

    [DisplayName("距離の数字の色"), Color]
    Vector4 numberColor = Vector4(0.944f, 0.413f, 0.053f, 1.0f);

    [DisplayName("見出しと木札の文字の色"), Color]
    Vector4 labelColor = Vector4(0.735f, 0.546f, 0.296f, 1.0f);

    [DisplayName("とっぱ・しんきろくの文字の色"), Color]
    Vector4 accentColor = Vector4(1.0f, 0.546f, 0.087f, 1.0f);

    [DisplayName("選んだとき一瞬縮める倍率"), Range(0.5, 1)]
    float reactionScale = 0.709f;

    [DisplayName("縮めて戻す時間（秒）"), Range(0.05, 1)]
    float reactionDuration = 0.156f;

    [DisplayName("選んでいる木札の倍率"), Range(1, 1.5)]
    float selectedScale = 1.156f;

    [DisplayName("決めたとき一瞬大きくする倍率"), Range(1, 3)]
    float confirmScale = 1.45f;

    [DisplayName("決めたときの演出の時間（秒）"), Range(0.05, 1.5)]
    float confirmDuration = 0.229f;

    [DisplayName("選ばなかった木札を縮める倍率"), Range(0.5, 1)]
    float unselectedFadeScale = 0.5f;

    [DisplayName("選ばなかった木札が消える時間（秒）"), Range(0.05, 1)]
    float unselectedFadeDuration = 0.087f;

    [DisplayName("選んでいる木札が上下する距離（px）"), Range(0, 40)]
    float selectedBobDistance = 9.6f;

    [DisplayName("選んでいる木札が上下する片道の時間（秒）"), Range(0.1, 3)]
    float selectedBobDuration = 0.634f;

    [DisplayName("選んでいる木札の文字の色"), Color]
    Vector4 selectedColor = Vector4(0.857723593711853f, 0.849984884262085f, 0.5404356122016907f, 1.0f);

    [DisplayName("記録のファイル（Application/Saved から）")]
    string recordPath = "Records/result_record.json";

    [DisplayName("ドット絵のフォントに付ける名前")]
    string pixelFontName = "ResultPixel";

    [DisplayName("Tips のフォント")]
    string tipFontName = "x8y12pxDenkiChip.ttf";

    private array<ResultPlank@> headline_ = array<ResultPlank@>(2);
    private array<UIImage@> headCreepers_ = array<UIImage@>(12);
    private array<UIImage@> headFoliage_ = array<UIImage@>(2);
    private array<UIImage@> headVines_ = array<UIImage@>(5);
    private UIText@ distanceText_;
    private UIText@ remainText_;

    private ResultPlank@ monkeyPlank_;
    private array<UIImage@> monkeyCreepers_ = array<UIImage@>(2);
    private array<UIImage@> monkeyVines_ = array<UIImage@>(2);
    private UIText@ monkeyText_;
    private ResultPlank@ rankPlank_;
    private UIText@ rankText_;
    private ResultPlank@ bestPlank_;
    private UIText@ bestText_;

    private UIImage@ railBar_;
    private array<UIImage@> sleepers_;
    private array<UIImage@> railVines_;
    private array<UIImage@> tickPosts_;
    private array<UIText@> tickTexts_;
    private UIImage@ gatePost_;
    private ResultPlank@ gateBeam_;
    private array<UIImage@> gateFoliage_ = array<UIImage@>(2);
    private UIText@ gateText_;
    private UIImage@ recordPost_;
    private ResultPlank@ recordPlank_;
    private UIText@ recordText_;
    private ResultPlank@ newRecordPlank_;
    private UIText@ newRecordText_;
    private UIImage@ cart_;

    private ResultChoice@ retry_;
    private ResultChoice@ title_;
    private UIImage@ cursor_;
    private UIText@ tipText_;
    private UIImage@ tipLeaf_;
    private UIImage@ tipBg_;
    private array<UIImage@> corners_ = array<UIImage@>(2);
    private array<ResultLeaf@> leaves_ = array<ResultLeaf@>(kGaugeLeafCount);

    private int runMeters_ = 0;
    private int previousMeters_ = 0;
    private int bestMeters_ = 0;
    private bool hasPrevious_ = false;
    private bool isNewBest_ = false;
    private float countTimer_ = 0.0f;
    private float shownMeters_ = 0.0f;
    private int lastTickIndex_ = -1;
    private bool arrived_ = false;
    private bool passedPrevious_ = false;
    private float swayTimer_ = 0.0f;
    private float exposureScale_ = 1.0f;
    private float cursorX_ = 0.0f;
    private bool built_ = false;
    private bool choiceColorApplied_ = false;
    private bool animatorsStarted_ = false;
    private int displayedMeters_ = -1;

    void Awake() override
    {
        BuildParts();
    }

    void Update() override
    {
        if (!built_) {
            return;
        }

        const float deltaTime = Clamp(Time::UnscaledDeltaTime(), 0.0f, kGaugeMaxStepSeconds);
        swayTimer_ += deltaTime;
        exposureScale_ = Clamp(exp2(-Rendering::GetAutoExposureEV()), kGaugeExposureScaleMin, kGaugeExposureScaleMax);

        // 選択肢の文字の色は、露出を打ち消した値を最初の更新で入れてから演出に控えさせる
        if (!choiceColorApplied_) {
            choiceColorApplied_ = true;
            if (retry_.text !is null) {
                retry_.text.color = Tinted(labelColor, 1.0f);
            }
            if (title_.text !is null) {
                title_.text.color = Tinted(labelColor, 1.0f);
            }
        }

        // 数えあげ
        const float total = Max(0.0f, countSeconds);
        const float target = float(runMeters_);
        if (countTimer_ < total) {
            countTimer_ += deltaTime;
            const float t = Clamp(total > 0.0f ? countTimer_ / total : 1.0f, 0.0f, 1.0f);
            shownMeters_ = target * Ease(EaseType::EaseOutCubic, t);
        } else {
            shownMeters_ = target;
        }

        // 目標の 2 割を越えるたびに、レールの音を上へずらしながら鳴らす
        const float tickStep = GoalDistance() * 0.2f;
        const int tickIndex = tickStep > 0.0f ? int(shownMeters_ / tickStep) : 0;
        if (tickIndex > lastTickIndex_) {
            if (lastTickIndex_ >= 0) {
                PlayParams params;
                params.bus = AudioBus::SE;
                params.volume = kGaugeSeTickVolume;
                params.pitch = 1.0f + 0.05f * float(tickIndex);
                Audio::PlayOneShot(kGaugeSeTick, params);
            }
            lastTickIndex_ = tickIndex;
        }

        // 前回の杭を追い越した瞬間
        if (hasPrevious_ && !passedPrevious_ && previousMeters_ > 0 && shownMeters_ >= float(previousMeters_)) {
            passedPrevious_ = true;
            BurstLeaves(Vector2(DistanceToX(float(previousMeters_)), kGaugeRailY - 60.0f), 5, 260.0f);
            PlayParams params;
            params.bus = AudioBus::SE;
            params.volume = 0.5f;
            params.pitch = 1.4f;
            Audio::PlayOneShot(kGaugeSePassRecord, params);
        }

        // 到着
        if (!arrived_ && shownMeters_ >= target - 0.001f) {
            arrived_ = true;
            const bool reached = float(runMeters_) >= GoalDistance();
            BurstLeaves(Vector2(DistanceToX(target), kGaugeCartY), reached ? 14 : 7, reached ? 460.0f : 300.0f);
            if (reached) {
                PlayParams params;
                params.bus = AudioBus::SE;
                params.volume = kGaugeSeArriveVolume;
                params.pitch = 1.0f;
                Audio::PlayOneShot(kGaugeSeArrive, params);
            }
        }

        ApplyLayout(deltaTime);
        UpdateLeaves(deltaTime);

        if (!animatorsStarted_) {
            animatorsStarted_ = true;
            if (retry_.animator !is null) {
                retry_.animator.Start();
            }
            if (title_.animator !is null) {
                title_.animator.Start();
            }
        }
    }

    // Tips の文字を替える（空なら隠す）
    void SetTipText(const string &in tip)
    {
        if (tipText_ is null) {
            return;
        }
        tipText_.text = tip;
        tipText_.gameObject.active = !tip.isEmpty();
    }

    // 選んでいる木札を替える。playReaction なら選んだ側に選んだときの演出を付ける
    void SetSelection(bool titleSelected, bool playReaction)
    {
        if (retry_.animator !is null) {
            retry_.animator.SetSelected(!titleSelected);
        }
        if (title_.animator !is null) {
            title_.animator.SetSelected(titleSelected);
        }
        if (!playReaction) {
            return;
        }

        ResultChoice@ selected = titleSelected ? title_ : retry_;
        if (selected.animator !is null) {
            selected.animator.PlaySelectionReaction();
        }
    }

    // 選ばなかった木札を消し、選んだ木札の決定の演出が終わったら onFinished を呼ぶ（演出が無ければすぐ呼ぶ）
    void Confirm(bool titleSelected, TweenCallback@ onFinished)
    {
        ResultChoice@ selected = titleSelected ? title_ : retry_;
        ResultChoice@ unselected = titleSelected ? retry_ : title_;
        if (selected.animator !is null) {
            if (unselected.animator !is null) {
                unselected.animator.PlayUnselectedFade();
            }
            selected.animator.PlayConfirmReaction(onFinished);
            return;
        }
        onFinished();
    }

    // 今回の距離を記録へ残してから、板・ツタ・レール・文字を作る
    private void BuildParts()
    {
        runMeters_ = Max(0, Session::GetInt("GameResult.HorizontalProgressBlocks", 0));

        SaveFile@ record = SaveFile(recordPath);
        const int storedLast = record.GetInt("lastMeters", 0);
        const int storedBest = record.GetInt("bestMeters", 0);
        const bool storedHasLast = record.GetBool("hasLast", false);
        record.SetInt("lastMeters", runMeters_);
        record.SetInt("bestMeters", Max(storedBest, runMeters_));
        record.SetBool("hasLast", true);
        record.Save();

        previousMeters_ = storedLast;
        bestMeters_ = Max(storedBest, runMeters_);
        hasPrevious_ = storedHasLast;
        isNewBest_ = runMeters_ > storedBest && storedHasLast;

        Font::Register(pixelFontName, kGaugePixelFontFile, kGaugePixelFontFamilies, kGaugePixelCharset);

        const int order = sortOrder;
        BuildGauge(order);
        BuildHeadline(order + 20);
        BuildSideBoards(order + 20);
        BuildChoices(order + 30);
        BuildFooter(order + 40);

        // 舞う葉はいちばん手前
        for (uint i = 0; i < leaves_.length(); ++i) {
            ResultLeaf leaf;
            @leaf.image = SpawnImage(Random::Range(0.0f, 1.0f) < 0.5f ? kGaugeTexLeafM : kGaugeTexLeafS, "ResultLeaf", order + 50);
            if (leaf.image !is null) {
                leaf.image.size = Vector2(kGaugeLeafWidth, kGaugeLeafHeight);
                leaf.image.gameObject.active = false;
            }
            @leaves_[i] = leaf;
        }

        // 上の 2 隅に茂みを置く
        for (uint i = 0; i < corners_.length(); ++i) {
            @corners_[i] = SpawnImage(kGaugeTexFoliage, "ResultCorner" + i, order + 10);
            if (corners_[i] is null) {
                continue;
            }
            const float scale = 1.15f;
            corners_[i].size = Vector2(kGaugeFoliageWidth * scale, kGaugeFoliageHeight * scale);
            corners_[i].anchoredPosition = Vector2(i == 0 ? -944.0f : 944.0f, 4.0f);
        }

        built_ = true;
        shownMeters_ = 0.0f;
        ApplyLayout(0.0f);
        SetSelection(false, false);
    }

    private UIImage@ SpawnImage(const string &in texture, const string &in name, int order)
    {
        UIImage@ image = owner.SpawnUIImage(texture, name);
        if (image is null) {
            return null;
        }
        image.anchor = UIAnchor::TopCenter;
        image.pivot = Vector2(0.5f, 0.5f);
        image.sortOrder = order;
        return image;
    }

    private UIText@ SpawnText(const string &in fontName, const string &in text, const string &in name, float fontSize, int order)
    {
        UIText@ label = owner.SpawnUIText(fontName, text, name);
        if (label is null) {
            return null;
        }
        label.anchor = UIAnchor::TopCenter;
        label.pivot = Vector2(0.5f, 0.5f);
        label.fontSize = fontSize;
        label.SetOutline(Vector4(0.0f, 0.0f, 0.0f, 1.0f), kGaugeOutlineWidth);
        label.sortOrder = order;
        return label;
    }

    private ResultPlank@ SpawnPlank(const string &in name, int order)
    {
        ResultPlank plank;
        @plank.mid = SpawnImage(kGaugeTexPlankMid, name + "Mid", order);
        @plank.capLeft = SpawnImage(kGaugeTexPlankCapL, name + "CapL", order + 1);
        @plank.capRight = SpawnImage(kGaugeTexPlankCapR, name + "CapR", order + 1);
        return plank;
    }

    // 見出しの 2 段の板・ツタ・茂みと、距離・目標までの残り・新記録の文字
    private void BuildHeadline(int order)
    {
        for (uint row = 0; row < headline_.length(); ++row) {
            @headline_[row] = SpawnPlank("ResultHeadRow" + row, order);
        }
        for (uint i = 0; i < headCreepers_.length(); ++i) {
            @headCreepers_[i] = SpawnImage(kGaugeTexVineH, "ResultHeadCreeper" + i, order + 4);
            if (headCreepers_[i] !is null) {
                headCreepers_[i].size = Vector2(kGaugeCreeperWidth, kGaugeCreeperHeight);
            }
        }
        for (uint i = 0; i < headFoliage_.length(); ++i) {
            @headFoliage_[i] = SpawnImage(kGaugeTexFoliage, "ResultHeadFoliage" + i, order + 5);
            if (headFoliage_[i] !is null) {
                headFoliage_[i].size = Vector2(kGaugeFoliageWidth * 0.85f, kGaugeFoliageHeight * 0.85f);
            }
        }
        for (uint i = 0; i < headVines_.length(); ++i) {
            @headVines_[i] = SpawnImage(kGaugeTexVineV, "ResultHeadVine" + i, order - 1);
            if (headVines_[i] !is null) {
                headVines_[i].size = Vector2(kGaugeVineWidth, kGaugeHeadTop + 8.0f);
                headVines_[i].anchoredPosition = Vector2(kGaugeHeadVineX[i], (kGaugeHeadTop + 8.0f) * 0.5f);
            }
        }

        @distanceText_ = SpawnText(pixelFontName, "0ｍ", "ResultDistance", kGaugeDistanceFontSize, order + 6);
        @remainText_ = SpawnText(pixelFontName, "", "ResultRemain", kGaugeRemainFontSize, order + 6);

        @newRecordPlank_ = SpawnPlank("ResultNewRecord", order + 2);
        @newRecordText_ = SpawnText(pixelFontName, "しんきろく！", "ResultNewRecordText", kGaugeNewRecordFontSize, order + 6);
    }

    // 左上のサルの数と称号の板、右上の自己最高の板
    private void BuildSideBoards(int order)
    {
        @monkeyPlank_ = SpawnPlank("ResultMonkeyBoard", order);
        for (uint i = 0; i < monkeyCreepers_.length(); ++i) {
            @monkeyCreepers_[i] = SpawnImage(kGaugeTexVineH, "ResultMonkeyCreeper" + i, order + 4);
            if (monkeyCreepers_[i] !is null) {
                monkeyCreepers_[i].size = Vector2(kGaugeCreeperWidth * 0.7f, kGaugeCreeperHeight * 0.7f);
            }
        }
        for (uint i = 0; i < monkeyVines_.length(); ++i) {
            @monkeyVines_[i] = SpawnImage(kGaugeTexVineV, "ResultMonkeyVine" + i, order - 1);
            if (monkeyVines_[i] !is null) {
                const float length = kGaugeSideY - kGaugePlankHeight * 0.5f + 8.0f;
                monkeyVines_[i].size = Vector2(kGaugeVineWidth, length);
                monkeyVines_[i].anchoredPosition = Vector2(
                    kGaugeSideCenterX + (i == 0 ? -kGaugeSideWidth * 0.28f : kGaugeSideWidth * 0.28f), length * 0.5f);
            }
        }

        const int monkeys = Session::GetInt("GameResult.MonkeyCount", 0);
        @monkeyText_ = SpawnText(pixelFontName, "つれてきたサル " + monkeys + "ひき", "ResultMonkeyCount", kGaugeSideFontSize, order + 6);

        @rankPlank_ = SpawnPlank("ResultRankBoard", order);
        const float ratio = float(runMeters_) / Max(1.0f, GoalDistance());
        @rankText_ = SpawnText(pixelFontName, "しょうごう  " + ResultGaugeRankName(ratio), "ResultRank", kGaugeRankFontSize, order + 6);

        @bestPlank_ = SpawnPlank("ResultBestBoard", order);
        @bestText_ = SpawnText(pixelFontName, "さいこうきろく " + bestMeters_ + "ｍ", "ResultBest", kGaugeBestFontSize, order + 6);
    }

    // レール・枕木・ツタ・目盛り・前回の杭・目標の看板・トロッコ
    private void BuildGauge(int order)
    {
        const float pitch = TiePitch();
        const int tieCount = int((kGaugeGoalX - kGaugeRailLeft) / pitch + 0.001f);
        for (int i = 1; i <= tieCount; ++i) {
            UIImage@ sleeper = SpawnImage(kGaugeTexPlankMid, "ResultSleeper", order);
            if (sleeper is null) {
                continue;
            }
            sleeper.size = Vector2(kGaugeSleeperWidth, kGaugeSleeperHeight);
            sleeper.anchoredPosition = Vector2(kGaugeRailLeft + pitch * float(i), kGaugeRailY);
            sleepers_.insertLast(sleeper);
        }

        // 走った区間のレールは、左端を基準に中板 1 枚を右へ伸ばす
        @railBar_ = SpawnImage(kGaugeTexPlankMid, "ResultRailBar", order + 1);
        if (railBar_ !is null) {
            railBar_.pivot = Vector2(0.0f, 0.5f);
            railBar_.size = Vector2(0.0f, kGaugeRailHeight);
            railBar_.anchoredPosition = Vector2(kGaugeRailLeft, kGaugeRailY);
        }

        for (float x = kGaugeRailLeft; x < kGaugeGoalX; x += kGaugeRailVineStep) {
            UIImage@ vine = SpawnImage(kGaugeTexVineH, "ResultRailVine", order + 2);
            if (vine is null) {
                continue;
            }
            vine.size = Vector2(kGaugeCreeperWidth * 0.6f, kGaugeCreeperHeight * 0.6f);
            vine.anchoredPosition = Vector2(x + 30.0f, kGaugeRailY - kGaugeRailHeight * 0.5f - 14.0f);
            vine.gameObject.active = false;
            railVines_.insertLast(vine);
        }

        // 目盛りは 100m ごとで、最後の 1 本は目標の地点
        const int tickCount = Clamp(int(GoalDistance() / kGaugeTickStepMeters + 0.5f), 1, kGaugeMaxTicks);
        for (int i = 0; i < tickCount; ++i) {
            UIImage@ post = SpawnImage(kGaugeTexPlankMid, "ResultTick" + i, order + 2);
            if (post !is null) {
                post.size = Vector2(kGaugeTickPostWidth, kGaugeTickPostHeight);
            }
            tickPosts_.insertLast(post);
            tickTexts_.insertLast(SpawnText(pixelFontName, "0", "ResultTickLabel" + i, kGaugeTickFontSize, order + 6));
        }

        @recordPost_ = SpawnImage(kGaugeTexPlankCapR, "ResultRecordPost", order - 1);
        if (recordPost_ !is null) {
            recordPost_.size = Vector2(kGaugeRecordPostWidth, kGaugeRecordPostBottom - kGaugeRecordPostTop);
        }
        @recordPlank_ = SpawnPlank("ResultRecordBoard", order + 3);
        @recordText_ = SpawnText(pixelFontName, "", "ResultRecordLabel", kGaugeRecordFontSize, order + 6);

        @gatePost_ = SpawnImage(kGaugeTexPlankCapR, "ResultGatePost", order - 1);
        if (gatePost_ !is null) {
            gatePost_.size = Vector2(kGaugeGatePostWidth, kGaugeRecordPostBottom - kGaugeGatePostTop);
            gatePost_.anchoredPosition = Vector2(kGaugeGoalX, (kGaugeGatePostTop + kGaugeRecordPostBottom) * 0.5f);
        }
        @gateBeam_ = SpawnPlank("ResultGateBeam", order + 4);
        for (uint i = 0; i < gateFoliage_.length(); ++i) {
            @gateFoliage_[i] = SpawnImage(kGaugeTexFoliage, "ResultGateFoliage" + i, order + 5);
            if (gateFoliage_[i] !is null) {
                gateFoliage_[i].size = Vector2(kGaugeFoliageWidth * 0.6f, kGaugeFoliageHeight * 0.6f);
            }
        }
        @gateText_ = SpawnText(pixelFontName, "もくひょう 0ｍ", "ResultGateLabel", kGaugeGateFontSize, order + 6);

        @cart_ = SpawnImage(kGaugeTexCart, "ResultCart", order + 7);
        if (cart_ !is null) {
            cart_.size = Vector2(kGaugeCartWidth, kGaugeCartHeight);
        }
    }

    // 「もういちど」と「タイトルへ」の木札と、選んでいる側を指す葉のカーソル
    private void BuildChoices(int order)
    {
        @retry_ = BuildChoice("もういちど", "ResultRetryButton", "result_retry_button", -kGaugeChoiceOffsetX, order);
        @title_ = BuildChoice("タイトルへ", "ResultTitleButton", "result_title_button", kGaugeChoiceOffsetX, order);

        @cursor_ = SpawnImage(kGaugeTexCursor, "ResultCursor", order + 7);
        if (cursor_ !is null) {
            cursor_.size = Vector2(kGaugeCursorWidth * 0.9f, kGaugeCursorHeight * 0.9f);
            cursor_.rotation = 0.10f;
        }
        cursorX_ = -kGaugeChoiceOffsetX;
    }

    private ResultChoice@ BuildChoice(const string &in label, const string &in name, const string &in tweenId, float centerX, int order)
    {
        ResultChoice choice;
        @choice.plank = SpawnPlank(name + "Plank", order);
        for (uint i = 0; i < choice.creepers.length(); ++i) {
            @choice.creepers[i] = SpawnImage(kGaugeTexVineH, name + "Creeper" + i, order + 4);
            if (choice.creepers[i] !is null) {
                choice.creepers[i].size = Vector2(kGaugeCreeperWidth * 0.8f, kGaugeCreeperHeight * 0.8f);
            }
        }
        choice.baseFontSize = kGaugeChoiceFontSize;
        choice.baseY = kGaugeChoiceY;
        @choice.text = SpawnText(pixelFontName, label, name, kGaugeChoiceFontSize, order + 6);
        if (choice.text !is null) {
            choice.text.anchoredPosition = Vector2(centerX, kGaugeChoiceY);
            choice.text.color = labelColor;
            @choice.animator = ResultButtonAnimator(choice.text, tweenId, this);
        }
        return choice;
    }

    // Tips の帯・文字・葉（最初は隠す）
    private void BuildFooter(int order)
    {
        @tipBg_ = SpawnImage(kGaugeTexDim, "ResultTipBg", order - 1);
        if (tipBg_ !is null) {
            tipBg_.anchoredPosition = Vector2(0.0f, kGaugeTipY);
            tipBg_.gameObject.active = false;
        }

        @tipText_ = SpawnText(tipFontName, "", "ResultTip", kGaugeTipFontSize, order);
        if (tipText_ !is null) {
            tipText_.anchoredPosition = Vector2(0.0f, kGaugeTipY);
            tipText_.SetAlign(TextAlignH::Center, TextAlignV::Middle);
            tipText_.gameObject.active = false;
        }

        @tipLeaf_ = SpawnImage(kGaugeTexLeafM, "ResultTipLeaf", order);
        if (tipLeaf_ !is null) {
            tipLeaf_.size = Vector2(kGaugeLeafWidth, kGaugeLeafHeight);
            tipLeaf_.gameObject.active = false;
        }
    }

    private float GoalDistance() const
    {
        return Max(1.0f, goalMeters);
    }

    // 目盛り 1 つ（100m）を等分した枕木 1 本ぶんの間隔（px）。詰まりすぎるときは等分する数を減らす
    private float TiePitch() const
    {
        const float tickSpan = (kGaugeGoalX - kGaugeRailLeft) * (kGaugeTickStepMeters / GoalDistance());
        int ties = kGaugeTiesPerTick;
        while (ties > 1 && tickSpan / float(ties) < kGaugeMinTiePitch) {
            --ties;
        }
        return tickSpan / float(ties);
    }

    // 距離（m）を画面の x（中央から）へ写す。目標を越えた分は看板の先へ少しだけはみ出させる
    private float DistanceToX(float meters) const
    {
        const float goal = GoalDistance();
        const float span = kGaugeGoalX - kGaugeRailLeft;
        if (meters <= goal) {
            return kGaugeRailLeft + (meters / goal) * span;
        }
        return kGaugeGoalX + Min(kGaugeOverGoalMaxX, (meters - goal) * 0.6f);
    }

    // 色に、自動露出を打ち消す倍率と scale を掛ける（アルファはそのまま）
    private Vector4 Tinted(const Vector4 &in color, float scale) const
    {
        const float gain = exposureScale_ * scale;
        return Vector4(color.x * gain, color.y * gain, color.z * gain, color.w);
    }

    private void PlacePlank(ResultPlank@ plank, const Vector2 &in center, float width, float height, float angle)
    {
        const float capOffset = (width - kGaugeCapWidth) * 0.5f;
        const float midWidth = Max(0.0f, width - kGaugeCapWidth * 2.0f);
        PlacePlankPart(plank.mid, center, 0.0f, midWidth, height, angle);
        PlacePlankPart(plank.capLeft, center, -capOffset, kGaugeCapWidth, height, angle);
        PlacePlankPart(plank.capRight, center, capOffset, kGaugeCapWidth, height, angle);
    }

    private void PlacePlankPart(UIImage@ image, const Vector2 &in center, float offsetX, float sizeX, float height, float angle)
    {
        if (image is null) {
            return;
        }
        const Vector2 offset = ResultGaugeRotateOffset(Vector2(offsetX, 0.0f), angle);
        image.anchoredPosition = Vector2(center.x + offset.x, center.y + offset.y);
        image.size = Vector2(sizeX, height);
        image.rotation = angle;
    }

    private void SetPlankColor(ResultPlank@ plank, const Vector4 &in color)
    {
        if (plank.mid !is null) {
            plank.mid.color = color;
        }
        if (plank.capLeft !is null) {
            plank.capLeft.color = color;
        }
        if (plank.capRight !is null) {
            plank.capRight.color = color;
        }
    }

    private void SetPlankActive(ResultPlank@ plank, bool active)
    {
        if (plank.mid !is null) {
            plank.mid.gameObject.active = active;
        }
        if (plank.capLeft !is null) {
            plank.capLeft.gameObject.active = active;
        }
        if (plank.capRight !is null) {
            plank.capRight.gameObject.active = active;
        }
    }

    // 見出し・左右の板・ゲージ・選択肢・カーソル・Tips を、今の距離と揺れに合わせて置き直す
    private void ApplyLayout(float deltaTime)
    {
        const Vector4 wood = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness);
        const Vector4 woodDim = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 0.55f);
        const float sway = sin(swayTimer_ * kGaugeSwaySpeed) * kGaugeSwayAngle;

        // 見出し
        for (uint row = 0; row < headline_.length(); ++row) {
            const float y = (row == 0) ? kGaugeHeadRow1Y : kGaugeHeadRow2Y;
            PlacePlank(headline_[row], Vector2(0.0f, y), kGaugeHeadWidth, kGaugePlankHeight, sway * 0.4f);
            SetPlankColor(headline_[row], wood);
        }
        const uint creeperCount = headCreepers_.length();
        for (uint i = 0; i < creeperCount; ++i) {
            if (headCreepers_[i] is null) {
                continue;
            }
            const bool bottom = i >= creeperCount / 2;
            const uint slot = bottom ? i - creeperCount / 2 : i;
            const float x = -kGaugeHeadWidth * 0.5f + 30.0f
                + float(slot) * (kGaugeHeadWidth - 60.0f) / 5.0f + (bottom ? 26.0f : 0.0f);
            headCreepers_[i].anchoredPosition = Vector2(
                x, bottom ? kGaugeHeadTop + kGaugePlankHeight * 2.0f - 4.0f : kGaugeHeadTop + 4.0f);
            headCreepers_[i].rotation = bottom ? 3.14159265f : 0.0f;
            headCreepers_[i].color = wood;
        }
        for (uint i = 0; i < headFoliage_.length(); ++i) {
            if (headFoliage_[i] is null) {
                continue;
            }
            headFoliage_[i].anchoredPosition = Vector2(
                (i == 0 ? -1.0f : 1.0f) * (kGaugeHeadWidth * 0.5f - 22.0f), kGaugeHeadTop + 10.0f);
            headFoliage_[i].rotation = sway * 1.5f;
            headFoliage_[i].color = wood;
        }
        for (uint i = 0; i < headVines_.length(); ++i) {
            if (headVines_[i] !is null) {
                headVines_[i].color = wood;
            }
        }

        const int meters = int(shownMeters_ + 0.5f);
        if (distanceText_ !is null && meters != displayedMeters_) {
            displayedMeters_ = meters;
            distanceText_.text = "" + meters + "ｍ";
        }
        if (distanceText_ !is null) {
            distanceText_.anchoredPosition = Vector2(0.0f, kGaugeHeadRow1Y);
            distanceText_.color = Tinted(numberColor, 1.0f);
        }
        if (remainText_ !is null) {
            const float goal = GoalDistance();
            const bool reached = shownMeters_ >= goal;
            const int diff = int(abs(shownMeters_ - goal) + 0.5f);
            remainText_.text = reached
                ? ("" + int(goal) + "ｍ とっぱ！  ＋" + diff + "ｍ")
                : ("もくひょうまで あと " + diff + "ｍ");
            remainText_.anchoredPosition = Vector2(0.0f, kGaugeHeadRow2Y);
            remainText_.color = Tinted(reached ? accentColor : labelColor, 1.0f);
        }

        // 新記録は、更新した回だけ到着してから出す
        const bool showNewRecord = isNewBest_ && arrived_;
        SetPlankActive(newRecordPlank_, showNewRecord);
        if (newRecordText_ !is null) {
            newRecordText_.gameObject.active = showNewRecord;
        }
        if (showNewRecord) {
            const float angle = kGaugeNewRecordRotation + sway * 2.0f;
            PlacePlank(newRecordPlank_, Vector2(kGaugeNewRecordX, kGaugeNewRecordY), kGaugeNewRecordWidth, kGaugePlankHeight, angle);
            SetPlankColor(newRecordPlank_, Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 1.3f));
            if (newRecordText_ !is null) {
                newRecordText_.anchoredPosition = Vector2(kGaugeNewRecordX, kGaugeNewRecordY);
                newRecordText_.rotation = angle;
                newRecordText_.color = Tinted(accentColor, 1.0f);
            }
        }

        // 左上の板
        PlacePlank(monkeyPlank_, Vector2(kGaugeSideCenterX, kGaugeSideY), kGaugeSideWidth, kGaugePlankHeight, 0.0f);
        SetPlankColor(monkeyPlank_, wood);
        for (uint i = 0; i < monkeyCreepers_.length(); ++i) {
            if (monkeyCreepers_[i] !is null) {
                monkeyCreepers_[i].anchoredPosition = Vector2(
                    kGaugeSideCenterX + (i == 0 ? -1.0f : 1.0f) * kGaugeSideWidth * 0.24f,
                    kGaugeSideY - kGaugePlankHeight * 0.5f + 4.0f);
                monkeyCreepers_[i].color = wood;
            }
        }
        for (uint i = 0; i < monkeyVines_.length(); ++i) {
            if (monkeyVines_[i] !is null) {
                monkeyVines_[i].color = wood;
            }
        }
        if (monkeyText_ !is null) {
            monkeyText_.anchoredPosition = Vector2(kGaugeSideCenterX, kGaugeSideY);
            monkeyText_.color = Tinted(labelColor, 1.0f);
        }
        PlacePlank(rankPlank_, Vector2(kGaugeSideCenterX, kGaugeRankY), kGaugeSideWidth, kGaugeRankHeight, 0.0f);
        SetPlankColor(rankPlank_, Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 1.15f));
        if (rankText_ !is null) {
            rankText_.anchoredPosition = Vector2(kGaugeSideCenterX, kGaugeRankY);
            rankText_.color = Tinted(accentColor, 1.0f);
        }

        // 右上の板
        PlacePlank(bestPlank_, Vector2(kGaugeBestCenterX, kGaugeBestY), kGaugeBestWidth, kGaugeBestHeight, 0.0f);
        SetPlankColor(bestPlank_, wood);
        if (bestText_ !is null) {
            bestText_.anchoredPosition = Vector2(kGaugeBestCenterX, kGaugeBestY);
            bestText_.color = Tinted(labelColor, 1.0f);
        }

        ApplyGauge();

        ApplyChoice(retry_);
        ApplyChoice(title_);

        // 葉のカーソルは、大きくなっているほうの木札へ寄せる
        const float selectedScaleClamped = Max(1.001f, selectedScale);
        const bool titleFocused = SelectionOf(title_, selectedScaleClamped) > SelectionOf(retry_, selectedScaleClamped);
        ResultChoice@ focused = titleFocused ? title_ : retry_;
        if (cursor_ !is null) {
            const float scale = focused.text !is null ? focused.text.fontSize / Max(1.0f, focused.baseFontSize) : 1.0f;
            const float centerX = titleFocused ? kGaugeChoiceOffsetX : -kGaugeChoiceOffsetX;
            const float targetX = centerX - kGaugeChoiceWidth * 0.5f * scale - kGaugeCursorGap;
            cursorX_ = deltaTime > 0.0f ? ResultGaugeFollow(cursorX_, targetX, kGaugeCursorFollow, deltaTime) : targetX;
            const float cursorY = (focused.text !is null ? focused.text.anchoredPosition.y : kGaugeChoiceY) + 4.0f;
            cursor_.anchoredPosition = Vector2(cursorX_, cursorY);
            const float alpha = focused.text !is null ? focused.text.color.w : 1.0f;
            cursor_.color = Tinted(Vector4(1.0f, 1.0f, 1.0f, alpha), brightness * 1.2f);
        }

        // Tips は明るさを脈打たせ、出ている間だけ葉と帯を出す
        if (tipText_ !is null) {
            const float pulsePhase = swayTimer_ * (6.2831853f / kGaugeTipPulseSeconds);
            const float pulse = kGaugeTipPulseMin + (1.0f - kGaugeTipPulseMin) * (0.5f + 0.5f * sin(pulsePhase));
            tipText_.color = Tinted(Vector4(0.62f, 0.68f, 0.55f, 0.95f), pulse);
            const bool visible = tipText_.gameObject.active;
            if (tipLeaf_ !is null) {
                tipLeaf_.gameObject.active = visible;
            }
            if (visible && tipLeaf_ !is null) {
                tipLeaf_.anchoredPosition = Vector2(-tipText_.measuredSize.x * 0.5f - 34.0f, kGaugeTipY + 2.0f);
                tipLeaf_.color = wood;
            }
            if (tipBg_ !is null) {
                tipBg_.gameObject.active = visible;
            }
            if (visible && tipBg_ !is null) {
                tipBg_.size = Vector2(kGaugeCanvasWidth, kGaugeTipBgHeight);
                tipBg_.color = Vector4(0.0f, 0.0f, 0.0f, kGaugeTipBgAlpha);
            }
        }
        for (uint i = 0; i < corners_.length(); ++i) {
            if (corners_[i] !is null) {
                corners_[i].color = woodDim;
            }
        }
    }

    // レール・枕木・ツタ・目盛り・前回の杭・目標の看板・トロッコを、表示中の距離に合わせる
    private void ApplyGauge()
    {
        const Vector4 wood = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness);
        const Vector4 woodDim = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 0.55f);
        const Vector4 cartColor = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), railBrightness);
        const float goal = GoalDistance();
        const float endX = DistanceToX(shownMeters_);
        const bool reached = shownMeters_ >= goal;

        if (railBar_ !is null) {
            const float length = Max(0.0f, endX - kGaugeRailLeft);
            railBar_.size = Vector2(length, kGaugeRailHeight);
            railBar_.color = wood;
        }
        for (uint i = 0; i < sleepers_.length(); ++i) {
            UIImage@ sleeper = sleepers_[i];
            if (sleeper is null) {
                continue;
            }
            sleeper.gameObject.active = sleeper.anchoredPosition.x > endX - 8.0f;
            sleeper.color = Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 0.72f);
        }
        for (uint i = 0; i < railVines_.length(); ++i) {
            UIImage@ vine = railVines_[i];
            if (vine is null) {
                continue;
            }
            vine.gameObject.active = vine.anchoredPosition.x < endX - 24.0f;
            vine.color = wood;
        }
        for (uint i = 0; i < tickPosts_.length(); ++i) {
            const bool isGoal = (i + 1 == tickPosts_.length());
            const float meters = isGoal ? goal : kGaugeTickStepMeters * float(i + 1);
            const bool visible = meters <= goal + 0.5f;
            const float x = DistanceToX(meters);
            const bool passed = shownMeters_ >= meters;
            if (tickPosts_[i] !is null) {
                tickPosts_[i].gameObject.active = visible;
                tickPosts_[i].anchoredPosition = Vector2(x, kGaugeTickPostY);
                tickPosts_[i].color = passed ? wood : woodDim;
            }
            if (tickTexts_[i] !is null) {
                tickTexts_[i].gameObject.active = visible;
                tickTexts_[i].text = "" + int(meters + 0.5f);
                tickTexts_[i].anchoredPosition = Vector2(x, kGaugeTickLabelY);
                const Vector4 tickColor = (isGoal && reached) ? accentColor
                    : (passed ? labelColor : Vector4(0.16f, 0.20f, 0.15f, 1.0f));
                tickTexts_[i].color = Tinted(tickColor, 1.0f);
            }
        }

        // 前回の記録の杭
        const bool showRecord = hasPrevious_ && previousMeters_ > 0 && showPreviousRecord;
        if (recordPost_ !is null) {
            recordPost_.gameObject.active = showRecord;
        }
        SetPlankActive(recordPlank_, showRecord);
        if (recordText_ !is null) {
            recordText_.gameObject.active = showRecord;
        }
        if (showRecord) {
            const float x = DistanceToX(float(previousMeters_));
            if (recordPost_ !is null) {
                recordPost_.anchoredPosition = Vector2(x, (kGaugeRecordPostTop + kGaugeRecordPostBottom) * 0.5f);
                recordPost_.color = wood;
            }
            // 札は杭の真上に置き、目標の看板に触れる手前で左に止める
            const float labelX = Min(x, kGaugeGoalX - kGaugeGateBeamWidth * 0.5f - kGaugeRecordPlankWidth * 0.5f - 10.0f);
            PlacePlank(recordPlank_, Vector2(labelX, kGaugeRecordPlankY), kGaugeRecordPlankWidth, kGaugeRecordPlankHeight, 0.0f);
            SetPlankColor(recordPlank_, wood);
            if (recordText_ !is null) {
                recordText_.text = "まえのきろく " + previousMeters_ + "ｍ";
                recordText_.anchoredPosition = Vector2(labelX, kGaugeRecordPlankY);
                recordText_.color = Tinted(Vector4(0.62f, 0.68f, 0.55f, 1.0f), 1.0f);
            }
        }

        // 目標の看板は、越えると傾いて明るくなる
        const Vector4 gateWood = reached
            ? Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 1.2f)
            : Tinted(Vector4(1.0f, 1.0f, 1.0f, 1.0f), brightness * 0.9f);
        if (gatePost_ !is null) {
            gatePost_.color = gateWood;
        }
        const float beamAngle = reached ? -0.055f : 0.0f;
        PlacePlank(gateBeam_, Vector2(kGaugeGoalX, kGaugeGateBeamY), kGaugeGateBeamWidth, kGaugePlankHeight, beamAngle);
        SetPlankColor(gateBeam_, gateWood);
        for (uint i = 0; i < gateFoliage_.length(); ++i) {
            if (gateFoliage_[i] !is null) {
                gateFoliage_[i].anchoredPosition = Vector2(kGaugeGoalX + (i == 0 ? -178.0f : 178.0f), kGaugeGateBeamY - 28.0f);
                gateFoliage_[i].color = wood;
            }
        }
        if (gateText_ !is null) {
            gateText_.text = "もくひょう " + int(goal + 0.5f) + "ｍ";
            gateText_.anchoredPosition = Vector2(kGaugeGoalX, kGaugeGateBeamY);
            gateText_.rotation = beamAngle;
            gateText_.color = Tinted(reached ? accentColor : numberColor, 1.0f);
        }
        if (cart_ !is null) {
            cart_.anchoredPosition = Vector2(endX - 4.0f, kGaugeCartY);
            cart_.color = cartColor;
        }
    }

    // 選択肢の木札の板とツタを、文字の位置・大きさ・アルファに合わせる（文字そのものは演出が動かす）
    private void ApplyChoice(ResultChoice@ choice)
    {
        if (choice.text is null) {
            return;
        }

        const Vector2 textPosition = choice.text.anchoredPosition;
        const float scale = choice.text.fontSize / Max(1.0f, choice.baseFontSize);
        const float selectedScaleClamped = Max(1.001f, selectedScale);
        const float selection = Clamp((scale - 1.0f) / (selectedScaleClamped - 1.0f), 0.0f, 1.0f);
        const float alpha = choice.text.color.w;
        const float angle = kGaugeChoiceTilt * selection;
        const float width = kGaugeChoiceWidth * scale;

        PlacePlank(choice.plank, textPosition, width, kGaugePlankHeight * scale, angle);
        const Vector4 color = Tinted(Vector4(1.0f, 1.0f, 1.0f, alpha), brightness * (1.0f + 0.28f * selection));
        SetPlankColor(choice.plank, color);

        for (uint i = 0; i < choice.creepers.length(); ++i) {
            if (choice.creepers[i] is null) {
                continue;
            }
            const Vector2 offset = ResultGaugeRotateOffset(
                Vector2((float(i) - 1.0f) * width * 0.30f, -kGaugePlankHeight * 0.5f * scale + 4.0f), angle);
            choice.creepers[i].anchoredPosition = Vector2(textPosition.x + offset.x, textPosition.y + offset.y);
            choice.creepers[i].rotation = angle;
            choice.creepers[i].color = color;
        }
    }

    // 文字の大きさから、選んでいる度合い（0〜1）を読む
    private float SelectionOf(ResultChoice@ choice, float selectedScaleClamped) const
    {
        if (choice.text is null) {
            return 0.0f;
        }
        const float ratio = choice.text.fontSize / Max(1.0f, choice.baseFontSize);
        return Clamp((ratio - 1.0f) / (selectedScaleClamped - 1.0f), 0.0f, 1.0f);
    }

    // 休んでいる葉を count 枚まで、origin から上へ散らす
    private void BurstLeaves(const Vector2 &in origin, int count, float power)
    {
        int spawned = 0;
        for (uint i = 0; i < leaves_.length(); ++i) {
            if (spawned >= count) {
                break;
            }
            ResultLeaf@ leaf = leaves_[i];
            if (leaf.image is null || leaf.life > 0.0f) {
                continue;
            }
            const float angle = Random::Range(-2.6f, -0.5f);
            const float speed = power * Random::Range(0.6f, 1.25f);
            const float offsetX = Random::Range(-26.0f, 26.0f);
            const float offsetY = Random::Range(-16.0f, 16.0f);
            leaf.position = Vector2(origin.x + offsetX, origin.y + offsetY);
            leaf.velocity = Vector2(cos(angle) * speed * 0.7f, sin(angle) * speed);
            leaf.rotation = Random::Range(-3.14f, 3.14f);
            leaf.spin = Random::Range(-6.0f, 6.0f);
            leaf.lifeSpan = Random::Range(0.8f, 1.5f);
            leaf.life = leaf.lifeSpan;
            leaf.size = Random::Range(0.8f, 1.4f);
            leaf.image.gameObject.active = true;
            ++spawned;
        }
    }

    // 飛んでいる葉を重力で落とし、寿命の終わりに消す
    private void UpdateLeaves(float deltaTime)
    {
        for (uint i = 0; i < leaves_.length(); ++i) {
            ResultLeaf@ leaf = leaves_[i];
            if (leaf.image is null || leaf.life <= 0.0f) {
                continue;
            }
            leaf.life -= deltaTime;
            if (leaf.life <= 0.0f) {
                leaf.image.gameObject.active = false;
                continue;
            }
            leaf.velocity.y += kGaugeLeafGravity * deltaTime;
            leaf.velocity.x *= 0.99f;
            leaf.position.x += leaf.velocity.x * deltaTime;
            leaf.position.y += leaf.velocity.y * deltaTime;
            leaf.rotation += leaf.spin * deltaTime;

            const float alpha = Clamp(leaf.life / kGaugeLeafFadeSeconds, 0.0f, 1.0f);
            leaf.image.anchoredPosition = leaf.position;
            leaf.image.size = Vector2(kGaugeLeafWidth * leaf.size, kGaugeLeafHeight * leaf.size);
            leaf.image.rotation = leaf.rotation;
            leaf.image.color = Tinted(Vector4(1.0f, 1.0f, 1.0f, alpha), brightness);
        }
    }
}
