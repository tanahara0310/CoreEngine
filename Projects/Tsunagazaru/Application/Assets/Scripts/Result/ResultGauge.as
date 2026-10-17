using namespace CoreEngine;

// リザルトの文字の色（元の値。露出を打ち消して描いていたので ToneMappedColor で画面の色にする）
const Vector4 kResultNumberColor = Vector4(0.944f, 0.413f, 0.053f, 1.0f);
const Vector4 kResultLabelColor = Vector4(0.735f, 0.546f, 0.296f, 1.0f);
const Vector4 kResultAccentColor = Vector4(1.000f, 0.546f, 0.087f, 1.0f);
const Vector4 kResultMutedColor = Vector4(0.62f, 0.68f, 0.55f, 1.0f);
const Vector4 kResultTickDimColor = Vector4(0.16f, 0.20f, 0.15f, 1.0f);

// リザルトの配置（基準解像度 1920x1080。基準は画面の上端の中央。x は中央から、y は上端から）
namespace ResultUi
{
    const float kPlankHeight = 96.0f;
    const float kCapWidth = 40.0f;
    const float kHeadTop = 44.0f;
    const float kHeadWidth = 780.0f;
    const float kHeadRow1Y = 92.0f;    // kHeadTop + 板の半分
    const float kHeadRow2Y = 188.0f;   // kHeadTop + 板 1.5 枚
    const float kSideCenterX = -644.0f;
    const float kSideWidth = 452.0f;
    const float kSideY = 92.0f;
    const float kRankY = 176.0f;
    const float kRankHeight = 76.0f;
    const float kBestCenterX = 644.0f;
    const float kNewRecordX = 470.0f;
    const float kNewRecordY = 206.0f;
    const float kNewRecordWidth = 300.0f;
    const float kNewRecordRotation = -0.13f;
    const float kRailY = 800.0f;       // レール上面（トロッコが載る線）
    const float kRailLeft = -760.0f;   // 0m
    const float kGoalX = 600.0f;       // 目標地点
    const float kTickStepMeters = 100.0f;
    const float kMinTilePitch = 48.0f;
    const float kRailVineStep = 96.0f;
    const float kTickLabelY = 858.0f;  // kRailY + 58
    const float kTickPostY = 818.0f;   // kRailY + 杭の高さの半分
    const float kGateBeamY = 638.0f;
    const float kGateBeamWidth = 350.0f;
    const float kGatePostTop = 686.0f; // kGateBeamY + 板の半分
    const float kCartWidth = 128.0f;   // Loading/monkey.png（64x68）の 2 倍
    const float kCartHeight = 136.0f;
    const float kCartY = 736.0f;       // kRailY + 車輪の沈み 4 - 高さの半分
    const float kOverGoalMaxX = 46.0f;
    const float kRecordPlankWidth = 300.0f;
    const float kRecordPlankHeight = 52.0f;
    const float kRecordPlankY = 634.0f; // トロッコの上端より上
    const float kRecordPostTop = 660.0f;
    const float kRecordPostBottom = 806.0f;
    const float kChoiceY = 940.0f;
    const float kChoiceOffsetX = 262.0f;
    const float kChoiceWidth = 420.0f;
    const float kChoiceFontSize = 44.0f;
    const float kChoiceTilt = -0.030f;
    const float kTipY = 1042.0f;
}

// 選択肢 1 つ（板・蔦・文字）
class ResultChoice
{
    SignPlank@ plank;
    array<UIImage@> creepers;
    UIText@ text;
    ResultButton@ button;
}

// リザルトの画面の UI 一式。上に距離の見出しと左右の板（連れてきたサル・称号・自己最高）、
// 下に 0m から目標までの線路のゲージ（トロッコが数えあげに合わせて走る・前回の杭・目標の看板）、
// 足元に「もういちど／タイトルへ」と Tips の帯。背景のサルが見える y = 250〜760 には何も置かない
class ResultGauge
{
    float goalMeters = 500.0f;
    float countSeconds = 1.15f;
    float brightness = 1.0f;
    float railBrightness = 0.32f;
    int sortOrder = 1500;
    bool showPreviousRecord = true;
    string font = "x8y12pxDenkiChip.ttf";

    ResultChoice retry;
    ResultChoice title;

    private GameObject@ owner_;
    private int runMeters_ = 0;
    private int previousMeters_ = 0;
    private int bestMeters_ = 0;
    private bool hasPrevious_ = false;
    private bool isNewBest_ = false;

    private array<SignPlank@> headline_;
    private array<UIImage@> headCreepers_;
    private array<UIImage@> headFoliage_;
    private array<UIImage@> headVines_;
    private UIText@ distanceText_;
    private UIText@ remainText_;
    private SignPlank@ newRecordPlank_;
    private UIText@ newRecordText_;
    private SignPlank@ monkeyPlank_;
    private array<UIImage@> monkeyCreepers_;
    private array<UIImage@> monkeyVines_;
    private UIText@ monkeyText_;
    private SignPlank@ rankPlank_;
    private UIText@ rankText_;
    private SignPlank@ bestPlank_;
    private UIText@ bestText_;
    private array<UIImage@> sleepers_;
    private UIImage@ railBar_;
    private array<UIImage@> railVines_;
    private array<UIImage@> tickPosts_;
    private array<UIText@> tickTexts_;
    private UIImage@ recordPost_;
    private SignPlank@ recordPlank_;
    private UIText@ recordText_;
    private UIImage@ gatePost_;
    private SignPlank@ gateBeam_;
    private array<UIImage@> gateFoliage_;
    private UIText@ gateText_;
    private UIImage@ cart_;
    private UIImage@ cursor_;
    private UIImage@ tipBg_;
    private UIText@ tipText_;
    private UIImage@ tipLeaf_;
    private array<UIImage@> corners_;
    private array<PauseLeaf@> leaves_;

    private float countTimer_ = 0.0f;
    private float shownMeters_ = 0.0f;
    private int lastTickIndex_ = -1;
    private bool passedPrevious_ = false;
    private bool arrived_ = false;
    private float swayTimer_ = 0.0f;
    private float cursorX_ = 0.0f;
    private int displayedMeters_ = -1;

    void Build(GameObject@ owner, int runMeters, int monkeyCount)
    {
        @owner_ = owner;
        // 記録は表示にかかわらず必ず残す
        runMeters_ = runMeters;
        ResultRecordEntry@ record = ResultRecord::CommitRun(runMeters_);
        previousMeters_ = record.previous;
        bestMeters_ = Max(record.best, runMeters_);
        hasPrevious_ = record.hasPrevious;
        isNewBest_ = record.isNewBest && record.hasPrevious;

        BuildGauge(sortOrder);
        BuildHeadline(sortOrder + 20);
        BuildSideBoards(sortOrder + 20, monkeyCount);
        BuildChoices(sortOrder + 30);
        BuildFooter(sortOrder + 40);

        // 舞う葉はいちばん手前
        for (int i = 0; i < 18; ++i) {
            PauseLeaf leaf;
            @leaf.image = SpawnImage(Random::Range(0.0f, 1.0f) < 0.5f ? kPauseLeafM : kPauseLeafS, "ResultLeaf", sortOrder + 50);
            leaf.image.size = Vector2(48.0f, 28.0f);
            leaf.image.gameObject.active = false;
            leaves_.insertLast(leaf);
        }
        // 上の 2 隅だけ茂みを置く（下の 2 隅は Tips の帯とぶつかる）
        for (int i = 0; i < 2; ++i) {
            UIImage@ corner = SpawnImage(kSignFoliage, "ResultCorner" + i, sortOrder + 10);
            corner.size = Vector2(208.0f * 1.15f, 128.0f * 1.15f);
            corner.anchoredPosition = Vector2(i == 0 ? -944.0f : 944.0f, 4.0f);
            corners_.insertLast(corner);
        }
        ApplyLayout(0.0f);
    }

    void SetTipText(const string &in tip)
    {
        tipText_.text = tip;
        tipText_.gameObject.active = tip.length() > 0;
    }

    void Tick(float deltaTime)
    {
        const float step = Clamp(deltaTime, 0.0f, 0.1f);
        swayTimer_ += step;

        // 0m から今回の距離まで数えあげる
        const float total = Max(0.0f, countSeconds);
        const float target = float(runMeters_);
        if (countTimer_ < total) {
            countTimer_ += step;
            const float t = Clamp(total > 0.0f ? countTimer_ / total : 1.0f, 0.0f, 1.0f);
            shownMeters_ = target * Ease(EaseType::EaseOutCubic, t);
        } else {
            shownMeters_ = target;
        }

        // 目標の 2 割ごとに、レールが伸びる音を上へずらしながら鳴らす
        const float tickStep = GoalDistance() * 0.2f;
        const int tickIndex = tickStep > 0.0f ? int(shownMeters_ / tickStep) : 0;
        if (tickIndex > lastTickIndex_) {
            if (lastTickIndex_ >= 0) {
                PlaySe("Application/Assets/Sounds/SE/rail_build.mp3", 0.35f, 1.0f + 0.05f * float(tickIndex));
            }
            lastTickIndex_ = tickIndex;
        }

        // 前回の杭を追い越した瞬間
        if (hasPrevious_ && !passedPrevious_ && previousMeters_ > 0 && shownMeters_ >= float(previousMeters_)) {
            passedPrevious_ = true;
            BurstLeaves(Vector2(DistanceToX(float(previousMeters_)), ResultUi::kRailY - 60.0f), 5, 260.0f);
            PlaySe("Application/Assets/Sounds/SE/build.mp3", 0.5f, 1.4f);
        }

        // 到着
        if (!arrived_ && shownMeters_ >= target - 0.001f) {
            arrived_ = true;
            const bool reached = float(runMeters_) >= GoalDistance();
            BurstLeaves(Vector2(DistanceToX(target), ResultUi::kCartY), reached ? 14 : 7, reached ? 460.0f : 300.0f);
            if (reached) {
                PlaySe("Application/Assets/Sounds/SE/title_bound.mp3", 0.7f, 1.0f);
            }
        }

        ApplyLayout(step);
        UpdateLeaves(step);
    }

    // ---------------------------------------------------------------- 作る

    private UIImage@ SpawnImage(const string &in texture, const string &in name, int order)
    {
        UIImage@ image = owner_.SpawnUIImage(texture, name);
        image.anchor = UIAnchor::TopCenter;
        image.pivot = Vector2(0.5f, 0.5f);
        image.sortOrder = order;
        return image;
    }

    private UIText@ SpawnText(const string &in text, const string &in name, float fontSize, int order)
    {
        UIText@ label = owner_.SpawnUIText(font, text, name);
        label.anchor = UIAnchor::TopCenter;
        label.pivot = Vector2(0.5f, 0.5f);
        label.fontSize = fontSize;
        label.SetAlign(TextAlignH::Center, TextAlignV::Middle);
        label.SetOutline(Vector4(0.0f, 0.0f, 0.0f, 1.0f), 0.05f);
        label.sortOrder = order;
        return label;
    }

    private SignPlank@ SpawnPlank(const string &in name, int order)
    {
        SignPlank plank;
        @plank.mid = SpawnImage(kSignPlankMid, name + "Mid", order);
        @plank.capLeft = SpawnImage(kSignPlankCapL, name + "CapL", order + 1);
        @plank.capRight = SpawnImage(kSignPlankCapR, name + "CapR", order + 1);
        return plank;
    }

    private void BuildHeadline(int order)
    {
        for (int row = 0; row < 2; ++row) {
            headline_.insertLast(SpawnPlank("ResultHeadRow" + row, order));
        }
        for (int i = 0; i < 12; ++i) {
            UIImage@ creeper = SpawnImage(kSignVineH, "ResultHeadCreeper" + i, order + 4);
            creeper.size = Vector2(128.0f, 48.0f);
            headCreepers_.insertLast(creeper);
        }
        for (int i = 0; i < 2; ++i) {
            UIImage@ leaf = SpawnImage(kSignFoliage, "ResultHeadFoliage" + i, order + 5);
            leaf.size = Vector2(208.0f * 0.85f, 128.0f * 0.85f);
            headFoliage_.insertLast(leaf);
        }
        const array<float> vineX = { -290.0f, -155.0f, 0.0f, 155.0f, 290.0f };
        for (uint i = 0; i < vineX.length(); ++i) {
            UIImage@ vine = SpawnImage(kSignVineV, "ResultHeadVine" + i, order - 1);
            vine.size = Vector2(32.0f, ResultUi::kHeadTop + 8.0f);
            vine.anchoredPosition = Vector2(vineX[i], (ResultUi::kHeadTop + 8.0f) * 0.5f);
            headVines_.insertLast(vine);
        }
        @distanceText_ = SpawnText("0ｍ", "ResultDistance", 84.0f, order + 6);
        @remainText_ = SpawnText("", "ResultRemain", 32.0f, order + 6);
        // 右肩の「しんきろく！」。更新した回だけ出す
        @newRecordPlank_ = SpawnPlank("ResultNewRecord", order + 2);
        @newRecordText_ = SpawnText("しんきろく！", "ResultNewRecordText", 40.0f, order + 6);
    }

    private void BuildSideBoards(int order, int monkeyCount)
    {
        @monkeyPlank_ = SpawnPlank("ResultMonkeyBoard", order);
        for (int i = 0; i < 2; ++i) {
            UIImage@ creeper = SpawnImage(kSignVineH, "ResultMonkeyCreeper" + i, order + 4);
            creeper.size = Vector2(128.0f * 0.7f, 48.0f * 0.7f);
            monkeyCreepers_.insertLast(creeper);
        }
        for (int i = 0; i < 2; ++i) {
            const float length = ResultUi::kSideY - ResultUi::kPlankHeight * 0.5f + 8.0f;
            UIImage@ vine = SpawnImage(kSignVineV, "ResultMonkeyVine" + i, order - 1);
            vine.size = Vector2(32.0f, length);
            vine.anchoredPosition = Vector2(ResultUi::kSideCenterX + (i == 0 ? -ResultUi::kSideWidth * 0.28f : ResultUi::kSideWidth * 0.28f),
                length * 0.5f);
            monkeyVines_.insertLast(vine);
        }
        @monkeyText_ = SpawnText("つれてきたサル " + monkeyCount + "ひき", "ResultMonkeyCount", 30.0f, order + 6);

        @rankPlank_ = SpawnPlank("ResultRankBoard", order);
        const float ratio = float(runMeters_) / Max(1.0f, GoalDistance());
        @rankText_ = SpawnText("しょうごう  " + RankName(ratio), "ResultRank", 26.0f, order + 6);

        // 自己最高は記録が無い回（今回が最高）でも出す
        @bestPlank_ = SpawnPlank("ResultBestBoard", order);
        @bestText_ = SpawnText("さいこうきろく " + bestMeters_ + "ｍ", "ResultBest", 30.0f, order + 6);
    }

    private void BuildGauge(int order)
    {
        // 線路は 100m の目盛りを等分した幅で敷き詰める（タイルの継ぎ目が目盛りの杭と重なる）
        const float pitch = RailTilePitch();
        const int tileCount = int(ceil((ResultUi::kGoalX - ResultUi::kRailLeft) / pitch - 0.001f));
        for (int i = 0; i < tileCount; ++i) {
            UIImage@ tile = SpawnImage("Application/Assets/Textures/Loading/rail.png", "ResultRailTile" + i, order);
            tile.pivot = Vector2(0.0f, 0.5f);
            tile.size = Vector2(pitch, pitch);
            tile.anchoredPosition = Vector2(ResultUi::kRailLeft + pitch * float(i), ResultUi::kRailY - RailTileCenterLift(pitch));
            sleepers_.insertLast(tile);
        }

        // 走った区間を光らせる帯（板の中央の版下を左端から伸ばす）
        @railBar_ = SpawnImage(kSignPlankMid, "ResultRailBar", order + 1);
        railBar_.pivot = Vector2(0.0f, 0.5f);
        railBar_.size = Vector2(0.0f, RailBarHeight(pitch));
        railBar_.anchoredPosition = Vector2(ResultUi::kRailLeft, ResultUi::kRailY + RailBarHeight(pitch) * 0.5f);

        // 敷いた線をなぞる蔦
        for (float x = ResultUi::kRailLeft; x < ResultUi::kGoalX; x += ResultUi::kRailVineStep) {
            UIImage@ vine = SpawnImage(kSignVineH, "ResultRailVine", order + 2);
            vine.size = Vector2(128.0f * 0.6f, 48.0f * 0.6f);
            vine.anchoredPosition = Vector2(x + 30.0f, ResultUi::kRailY - 14.0f);
            vine.gameObject.active = false;
            railVines_.insertLast(vine);
        }

        // 目盛り（100m ごと。最後の 1 本は目標地点）
        const int tickCount = Clamp(int(GoalDistance() / ResultUi::kTickStepMeters + 0.5f), 1, 16);
        for (int i = 0; i < tickCount; ++i) {
            UIImage@ post = SpawnImage(kSignPlankMid, "ResultTick" + i, order + 2);
            post.size = Vector2(15.0f, 36.0f);
            tickPosts_.insertLast(post);
            tickTexts_.insertLast(SpawnText("0", "ResultTickLabel" + i, 24.0f, order + 6));
        }

        // 前回の記録の杭（レールより後ろ）
        @recordPost_ = SpawnImage(kSignPlankCapR, "ResultRecordPost", order - 1);
        recordPost_.size = Vector2(20.0f, ResultUi::kRecordPostBottom - ResultUi::kRecordPostTop);
        @recordPlank_ = SpawnPlank("ResultRecordBoard", order + 3);
        @recordText_ = SpawnText("", "ResultRecordLabel", 24.0f, order + 6);

        // 目標の看板と柱（柱はレールより後ろ）
        @gatePost_ = SpawnImage(kSignPlankCapR, "ResultGatePost", order - 1);
        gatePost_.size = Vector2(40.0f, ResultUi::kRecordPostBottom - ResultUi::kGatePostTop);
        gatePost_.anchoredPosition = Vector2(ResultUi::kGoalX, (ResultUi::kGatePostTop + ResultUi::kRecordPostBottom) * 0.5f);
        @gateBeam_ = SpawnPlank("ResultGateBeam", order + 4);
        for (int i = 0; i < 2; ++i) {
            UIImage@ leaf = SpawnImage(kSignFoliage, "ResultGateFoliage" + i, order + 5);
            leaf.size = Vector2(208.0f * 0.6f, 128.0f * 0.6f);
            gateFoliage_.insertLast(leaf);
        }
        @gateText_ = SpawnText("もくひょう 0ｍ", "ResultGateLabel", 30.0f, order + 6);

        @cart_ = SpawnImage("Application/Assets/Textures/Loading/monkey.png", "ResultCart", order + 7);
        cart_.size = Vector2(ResultUi::kCartWidth, ResultUi::kCartHeight);
    }

    private void BuildChoices(int order)
    {
        BuildChoice(retry, "もういちど", "ResultRetryButton", "result_retry_button", -ResultUi::kChoiceOffsetX, order);
        BuildChoice(title, "タイトルへ", "ResultTitleButton", "result_title_button", ResultUi::kChoiceOffsetX, order);
        @cursor_ = SpawnImage(kPauseCursor, "ResultCursor", order + 7);
        cursor_.size = Vector2(128.0f * 0.9f, 64.0f * 0.9f);
        cursor_.rotation = 0.10f;
        cursorX_ = -ResultUi::kChoiceOffsetX;
    }

    private void BuildChoice(ResultChoice@ choice, const string &in label, const string &in name, const string &in tweenId,
        float centerX, int order)
    {
        @choice.plank = SpawnPlank(name + "Plank", order);
        for (int i = 0; i < 3; ++i) {
            UIImage@ creeper = SpawnImage(kSignVineH, name + "Creeper" + i, order + 4);
            creeper.size = Vector2(128.0f * 0.8f, 48.0f * 0.8f);
            choice.creepers.insertLast(creeper);
        }
        @choice.text = SpawnText(label, name, ResultUi::kChoiceFontSize, order + 6);
        choice.text.anchoredPosition = Vector2(centerX, ResultUi::kChoiceY);
        choice.text.color = ToneMappedColor(kResultLabelColor);
        @choice.button = ResultButton(choice.text, tweenId);
    }

    private void BuildFooter(int order)
    {
        // Tips を読みやすくする半透明の黒い帯（文字より後ろ）
        @tipBg_ = SpawnImage(kPauseDim, "ResultTipBg", order - 1);
        tipBg_.anchoredPosition = Vector2(0.0f, ResultUi::kTipY);
        tipBg_.gameObject.active = false;
        @tipText_ = SpawnText("", "ResultTip", 34.0f, order);
        tipText_.anchoredPosition = Vector2(0.0f, ResultUi::kTipY);
        tipText_.gameObject.active = false;
        @tipLeaf_ = SpawnImage(kPauseLeafM, "ResultTipLeaf", order);
        tipLeaf_.size = Vector2(48.0f, 28.0f);
        tipLeaf_.gameObject.active = false;
    }

    // ---------------------------------------------------------------- 値

    private float GoalDistance() const
    {
        return Max(1.0f, goalMeters);
    }

    // 目盛り 1 つ（100m）の px を等分する。詰まりすぎるときだけ枚数を落とす
    private float RailTilePitch() const
    {
        const float tickSpan = (ResultUi::kGoalX - ResultUi::kRailLeft) * (ResultUi::kTickStepMeters / GoalDistance());
        int tiles = 2;
        while (tiles > 1 && tickSpan / float(tiles) < ResultUi::kMinTilePitch) {
            --tiles;
        }
        return tickSpan / float(tiles);
    }

    // 線路の版下（64x64）のうちレール上面は上から 56 テクセル。中心をその差ぶん持ち上げる
    private float RailTileCenterLift(float pitch) const
    {
        return pitch * (56.0f / 64.0f - 0.5f);
    }

    private float RailBarHeight(float pitch) const
    {
        return pitch * (4.0f / 64.0f);
    }

    // 目標を越えたぶんは、看板の先に少しだけはみ出させる
    private float DistanceToX(float meters) const
    {
        const float goal = GoalDistance();
        const float span = ResultUi::kGoalX - ResultUi::kRailLeft;
        if (meters <= goal) {
            return ResultUi::kRailLeft + (meters / goal) * span;
        }
        return ResultUi::kGoalX + Min(ResultUi::kOverGoalMaxX, (meters - goal) * 0.6f);
    }

    private Vector4 Wood(float scale, float alpha = 1.0f) const
    {
        const float gain = brightness * scale;
        return OriginalTextureTint(kTexMeanPlank, Vector4(gain, gain, gain, alpha));
    }

    private Vector4 Plant(float scale, float alpha = 1.0f) const
    {
        const float gain = brightness * scale;
        return OriginalTextureTint(kTexMeanPlant, Vector4(gain, gain, gain, alpha));
    }

    private Vector4 TextColor(const Vector4 &in color, float scale = 1.0f) const
    {
        const Vector4 mapped = ToneMappedColor(Vector4(color.x * scale, color.y * scale, color.z * scale, color.w));
        return mapped;
    }

    // ---------------------------------------------------------------- 置く

    private void PlacePlank(SignPlank@ plank, const Vector2 &in center, float width, float height, float angle = 0.0f)
    {
        const float capOffset = (width - ResultUi::kCapWidth) * 0.5f;
        PlacePart(plank.mid, center, 0.0f, Max(0.0f, width - ResultUi::kCapWidth * 2.0f), height, angle);
        PlacePart(plank.capLeft, center, -capOffset, ResultUi::kCapWidth, height, angle);
        PlacePart(plank.capRight, center, capOffset, ResultUi::kCapWidth, height, angle);
    }

    private void PlacePart(UIImage@ image, const Vector2 &in center, float offsetX, float sizeX, float sizeY, float angle)
    {
        const Vector2 offset = RotateOffset(Vector2(offsetX, 0.0f), angle);
        image.anchoredPosition = Vector2(center.x + offset.x, center.y + offset.y);
        image.size = Vector2(sizeX, sizeY);
        image.rotation = angle;
    }

    private void SetPlankColor(SignPlank@ plank, const Vector4 &in color)
    {
        plank.mid.color = color;
        plank.capLeft.color = color;
        plank.capRight.color = color;
    }

    private void SetPlankActive(SignPlank@ plank, bool active)
    {
        SetActive(plank.mid, active);
        SetActive(plank.capLeft, active);
        SetActive(plank.capRight, active);
    }

    private void SetActive(UIImage@ image, bool active)
    {
        if (image.gameObject.active != active) {
            image.gameObject.active = active;
        }
    }

    private void SetActive(UIText@ text, bool active)
    {
        if (text.gameObject.active != active) {
            text.gameObject.active = active;
        }
    }

    private void ApplyLayout(float deltaTime)
    {
        const Vector4 wood = Wood(1.0f);
        const Vector4 plant = Plant(1.0f);
        const float sway = sin(swayTimer_ * 1.4f) * 0.012f;

        // 見出し（空だけを覆う 2 段）
        for (uint row = 0; row < headline_.length(); ++row) {
            PlacePlank(headline_[row], Vector2(0.0f, row == 0 ? ResultUi::kHeadRow1Y : ResultUi::kHeadRow2Y), ResultUi::kHeadWidth,
                ResultUi::kPlankHeight, sway * 0.4f);
            SetPlankColor(headline_[row], wood);
        }
        const uint half = headCreepers_.length() / 2;
        for (uint i = 0; i < headCreepers_.length(); ++i) {
            const bool bottom = i >= half;
            const uint slot = bottom ? i - half : i;
            const float x = -ResultUi::kHeadWidth * 0.5f + 30.0f + float(slot) * (ResultUi::kHeadWidth - 60.0f) / 5.0f + (bottom ? 26.0f : 0.0f);
            headCreepers_[i].anchoredPosition = Vector2(x, bottom ? ResultUi::kHeadTop + ResultUi::kPlankHeight * 2.0f - 4.0f : ResultUi::kHeadTop + 4.0f);
            headCreepers_[i].rotation = bottom ? kPi : 0.0f;
            headCreepers_[i].color = plant;
        }
        for (uint i = 0; i < headFoliage_.length(); ++i) {
            headFoliage_[i].anchoredPosition = Vector2((i == 0 ? -1.0f : 1.0f) * (ResultUi::kHeadWidth * 0.5f - 22.0f), ResultUi::kHeadTop + 10.0f);
            headFoliage_[i].rotation = sway * 1.5f;
            headFoliage_[i].color = plant;
        }
        for (uint i = 0; i < headVines_.length(); ++i) {
            headVines_[i].color = plant;
        }

        const int meters = int(shownMeters_ + 0.5f);
        if (meters != displayedMeters_) {
            displayedMeters_ = meters;
            distanceText_.text = meters + "ｍ";
        }
        distanceText_.anchoredPosition = Vector2(0.0f, ResultUi::kHeadRow1Y);
        distanceText_.color = TextColor(kResultNumberColor);
        const float goal = GoalDistance();
        const bool reached = shownMeters_ >= goal;
        const int diff = int(abs(shownMeters_ - goal) + 0.5f);
        const string remain = reached ? (int(goal) + "ｍ とっぱ！  ＋" + diff + "ｍ") : ("もくひょうまで あと " + diff + "ｍ");
        if (remainText_.text != remain) {
            remainText_.text = remain;
        }
        remainText_.anchoredPosition = Vector2(0.0f, ResultUi::kHeadRow2Y);
        remainText_.color = TextColor(reached ? kResultAccentColor : kResultLabelColor);

        // 「しんきろく！」は更新した回だけ、到着してから出す
        const bool showNewRecord = isNewBest_ && arrived_;
        SetPlankActive(newRecordPlank_, showNewRecord);
        SetActive(newRecordText_, showNewRecord);
        if (showNewRecord) {
            const float angle = ResultUi::kNewRecordRotation + sway * 2.0f;
            PlacePlank(newRecordPlank_, Vector2(ResultUi::kNewRecordX, ResultUi::kNewRecordY), ResultUi::kNewRecordWidth,
                ResultUi::kPlankHeight, angle);
            SetPlankColor(newRecordPlank_, Wood(1.3f));
            newRecordText_.anchoredPosition = Vector2(ResultUi::kNewRecordX, ResultUi::kNewRecordY);
            newRecordText_.rotation = angle;
            newRecordText_.color = TextColor(kResultAccentColor);
        }

        // 左上の板（連れてきたサル・称号）
        PlacePlank(monkeyPlank_, Vector2(ResultUi::kSideCenterX, ResultUi::kSideY), ResultUi::kSideWidth, ResultUi::kPlankHeight);
        SetPlankColor(monkeyPlank_, wood);
        for (uint i = 0; i < monkeyCreepers_.length(); ++i) {
            monkeyCreepers_[i].anchoredPosition = Vector2(ResultUi::kSideCenterX + (i == 0 ? -1.0f : 1.0f) * ResultUi::kSideWidth * 0.24f,
                ResultUi::kSideY - ResultUi::kPlankHeight * 0.5f + 4.0f);
            monkeyCreepers_[i].color = plant;
        }
        for (uint i = 0; i < monkeyVines_.length(); ++i) {
            monkeyVines_[i].color = plant;
        }
        monkeyText_.anchoredPosition = Vector2(ResultUi::kSideCenterX, ResultUi::kSideY);
        monkeyText_.color = TextColor(kResultLabelColor);
        PlacePlank(rankPlank_, Vector2(ResultUi::kSideCenterX, ResultUi::kRankY), ResultUi::kSideWidth, ResultUi::kRankHeight);
        SetPlankColor(rankPlank_, Wood(1.15f));
        rankText_.anchoredPosition = Vector2(ResultUi::kSideCenterX, ResultUi::kRankY);
        rankText_.color = TextColor(kResultAccentColor);

        // 右上の板（自己最高）
        PlacePlank(bestPlank_, Vector2(ResultUi::kBestCenterX, ResultUi::kSideY), ResultUi::kSideWidth, ResultUi::kPlankHeight);
        SetPlankColor(bestPlank_, wood);
        bestText_.anchoredPosition = Vector2(ResultUi::kBestCenterX, ResultUi::kSideY);
        bestText_.color = TextColor(kResultLabelColor);

        ApplyGauge();

        // 選択肢と足元
        ApplyChoice(retry);
        ApplyChoice(title);

        // 葉のカーソルは大きくなっている方の札へ寄る
        const bool titleFocused = SelectionOf(title) > SelectionOf(retry);
        ResultChoice@ focused = retry;
        if (titleFocused) {
            @focused = title;
        }
        const float focusedScale = focused.text.fontSize / ResultUi::kChoiceFontSize;
        const float centerX = titleFocused ? ResultUi::kChoiceOffsetX : -ResultUi::kChoiceOffsetX;
        const float targetX = centerX - ResultUi::kChoiceWidth * 0.5f * focusedScale - 34.0f;
        cursorX_ = deltaTime > 0.0f ? Follow(cursorX_, targetX, 22.0f, deltaTime) : targetX;
        cursor_.anchoredPosition = Vector2(cursorX_, focused.text.anchoredPosition.y + 4.0f);
        const float cursorGain = brightness * 1.2f;
        cursor_.color = OriginalTextureTint(kTexMeanCursor, Vector4(cursorGain, cursorGain, cursorGain, focused.text.color.w));

        // Tips は明るさの山から暗い側へだけ振って明滅させる
        const float pulse = 0.30f + 0.70f * (0.5f + 0.5f * sin(swayTimer_ * (kTau / 1.6f)));
        const Vector4 tipColor = TextColor(kResultMutedColor, pulse);
        tipText_.color = Vector4(tipColor.x, tipColor.y, tipColor.z, 0.95f);
        const bool visible = tipText_.gameObject.active;
        SetActive(tipLeaf_, visible);
        if (visible) {
            tipLeaf_.anchoredPosition = Vector2(-tipText_.measuredSize.x * 0.5f - 34.0f, ResultUi::kTipY + 2.0f);
            tipLeaf_.color = plant;
        }
        // 帯は画面の左右端まで通す
        SetActive(tipBg_, visible);
        if (visible) {
            tipBg_.size = Vector2(1920.0f, 64.0f);
            tipBg_.color = Vector4(0.0f, 0.0f, 0.0f, 0.5f);
        }
        for (uint i = 0; i < corners_.length(); ++i) {
            corners_[i].color = Plant(0.55f);
        }
    }

    private void ApplyGauge()
    {
        const Vector4 wood = Wood(1.0f);
        const Vector4 woodDim = Wood(0.55f);
        const float goal = GoalDistance();
        const float endX = DistanceToX(shownMeters_);
        const bool reached = shownMeters_ >= goal;

        // 敷いたレール（帯を左端から現在地まで伸ばす）
        const float pitch = RailTilePitch();
        railBar_.size = Vector2(Max(0.0f, endX - ResultUi::kRailLeft), RailBarHeight(pitch));
        railBar_.color = wood;
        // 線路は最後まで見せたまま、走った区間だけ明るくする
        const Vector4 railLit = OriginalTextureTint(kTexMeanRail, Vector4(brightness, brightness, brightness, 1.0f));
        const Vector4 railDim = OriginalTextureTint(kTexMeanRail, Vector4(brightness * 0.55f, brightness * 0.55f, brightness * 0.55f, 1.0f));
        for (uint i = 0; i < sleepers_.length(); ++i) {
            sleepers_[i].color = sleepers_[i].anchoredPosition.x + pitch <= endX ? railLit : railDim;
        }
        const Vector4 plant = Plant(1.0f);
        for (uint i = 0; i < railVines_.length(); ++i) {
            SetActive(railVines_[i], railVines_[i].anchoredPosition.x < endX - 24.0f);
            railVines_[i].color = plant;
        }
        // 目盛り（最後の 1 本は目標地点。越えたら数字の色を変える）
        for (uint i = 0; i < tickPosts_.length(); ++i) {
            const bool isGoal = i + 1 == tickPosts_.length();
            const float meters = isGoal ? goal : ResultUi::kTickStepMeters * float(i + 1);
            const bool visible = meters <= goal + 0.5f;
            const float x = DistanceToX(meters);
            const bool passed = shownMeters_ >= meters;
            SetActive(tickPosts_[i], visible);
            tickPosts_[i].anchoredPosition = Vector2(x, ResultUi::kTickPostY);
            tickPosts_[i].color = passed ? wood : woodDim;
            SetActive(tickTexts_[i], visible);
            const string label = "" + int(meters + 0.5f);
            if (tickTexts_[i].text != label) {
                tickTexts_[i].text = label;
            }
            tickTexts_[i].anchoredPosition = Vector2(x, ResultUi::kTickLabelY);
            tickTexts_[i].color = TextColor((isGoal && reached) ? kResultAccentColor : passed ? kResultLabelColor : kResultTickDimColor);
        }

        // 前回の記録の杭
        const bool showRecord = hasPrevious_ && previousMeters_ > 0 && showPreviousRecord;
        SetActive(recordPost_, showRecord);
        SetPlankActive(recordPlank_, showRecord);
        SetActive(recordText_, showRecord);
        if (showRecord) {
            const float x = DistanceToX(float(previousMeters_));
            recordPost_.anchoredPosition = Vector2(x, (ResultUi::kRecordPostTop + ResultUi::kRecordPostBottom) * 0.5f);
            recordPost_.color = wood;
            // 札は杭の真上。目標の看板に触れる手前で左へ逃がす
            const float labelX = Min(x, ResultUi::kGoalX - ResultUi::kGateBeamWidth * 0.5f - ResultUi::kRecordPlankWidth * 0.5f - 10.0f);
            PlacePlank(recordPlank_, Vector2(labelX, ResultUi::kRecordPlankY), ResultUi::kRecordPlankWidth, ResultUi::kRecordPlankHeight);
            SetPlankColor(recordPlank_, wood);
            const string label = "まえのきろく " + previousMeters_ + "ｍ";
            if (recordText_.text != label) {
                recordText_.text = label;
            }
            recordText_.anchoredPosition = Vector2(labelX, ResultUi::kRecordPlankY);
            recordText_.color = TextColor(kResultMutedColor);
        }

        // 目標の看板。越えると板が傾いて明るくなる
        const Vector4 gateWood = reached ? Wood(1.2f) : Wood(0.9f);
        gatePost_.color = gateWood;
        const float beamAngle = reached ? -0.055f : 0.0f;
        PlacePlank(gateBeam_, Vector2(ResultUi::kGoalX, ResultUi::kGateBeamY), ResultUi::kGateBeamWidth, ResultUi::kPlankHeight, beamAngle);
        SetPlankColor(gateBeam_, gateWood);
        for (uint i = 0; i < gateFoliage_.length(); ++i) {
            gateFoliage_[i].anchoredPosition = Vector2(ResultUi::kGoalX + (i == 0 ? -178.0f : 178.0f), ResultUi::kGateBeamY - 28.0f);
            gateFoliage_[i].color = plant;
        }
        const string gateLabel = "もくひょう " + int(goal + 0.5f) + "ｍ";
        if (gateText_.text != gateLabel) {
            gateText_.text = gateLabel;
        }
        gateText_.anchoredPosition = Vector2(ResultUi::kGoalX, ResultUi::kGateBeamY);
        gateText_.rotation = beamAngle;
        gateText_.color = TextColor(reached ? kResultAccentColor : kResultNumberColor);

        cart_.anchoredPosition = Vector2(endX - 4.0f, ResultUi::kCartY);
        cart_.color = OriginalTextureTint(kTexMeanLoadingMonkey, Vector4(railBrightness, railBrightness, railBrightness, 1.0f));
    }

    // 選んでいる度合い（文字の大きさから読む。0 = 選んでいない ／ 1 = 選んでいる）
    private float SelectionOf(ResultChoice@ choice) const
    {
        const float selectedScale = Max(1.001f, choice.button.selectedScale);
        const float ratio = choice.text.fontSize / ResultUi::kChoiceFontSize;
        return Clamp((ratio - 1.0f) / (selectedScale - 1.0f), 0.0f, 1.0f);
    }

    // 板と蔦は文字に従う（文字は ResultButton が動かす）
    private void ApplyChoice(ResultChoice@ choice)
    {
        const Vector2 textPosition = choice.text.anchoredPosition;
        const float scale = choice.text.fontSize / ResultUi::kChoiceFontSize;
        const float selection = SelectionOf(choice);
        const float alpha = choice.text.color.w;
        const float angle = ResultUi::kChoiceTilt * selection;
        const float width = ResultUi::kChoiceWidth * scale;
        PlacePlank(choice.plank, textPosition, width, ResultUi::kPlankHeight * scale, angle);
        SetPlankColor(choice.plank, Wood(1.0f + 0.28f * selection, alpha));
        const Vector4 creeperColor = Plant(1.0f + 0.28f * selection, alpha);
        for (uint i = 0; i < choice.creepers.length(); ++i) {
            const Vector2 offset = RotateOffset(Vector2((float(i) - 1.0f) * width * 0.30f, -ResultUi::kPlankHeight * 0.5f * scale + 4.0f), angle);
            choice.creepers[i].anchoredPosition = Vector2(textPosition.x + offset.x, textPosition.y + offset.y);
            choice.creepers[i].rotation = angle;
            choice.creepers[i].color = creeperColor;
        }
    }

    // ---------------------------------------------------------------- 舞う葉

    private void BurstLeaves(const Vector2 &in origin, int count, float power)
    {
        int spawned = 0;
        for (uint i = 0; i < leaves_.length() && spawned < count; ++i) {
            PauseLeaf@ leaf = leaves_[i];
            if (leaf.life > 0.0f) {
                continue;
            }
            const float angle = Random::Range(-2.6f, -0.5f);
            const float speed = power * Random::Range(0.6f, 1.25f);
            leaf.position = Vector2(origin.x + Random::Range(-26.0f, 26.0f), origin.y + Random::Range(-16.0f, 16.0f));
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

    private void UpdateLeaves(float deltaTime)
    {
        for (uint i = 0; i < leaves_.length(); ++i) {
            PauseLeaf@ leaf = leaves_[i];
            if (leaf.life <= 0.0f) {
                continue;
            }
            leaf.life -= deltaTime;
            if (leaf.life <= 0.0f) {
                leaf.image.gameObject.active = false;
                continue;
            }
            leaf.velocity.y += 900.0f * deltaTime;
            leaf.velocity.x *= 0.99f;
            leaf.position.x += leaf.velocity.x * deltaTime;
            leaf.position.y += leaf.velocity.y * deltaTime;
            leaf.rotation += leaf.spin * deltaTime;
            leaf.image.anchoredPosition = leaf.position;
            leaf.image.size = Vector2(48.0f * leaf.size, 28.0f * leaf.size);
            leaf.image.rotation = leaf.rotation;
            leaf.image.color = Plant(1.0f, Clamp(leaf.life / 0.35f, 0.0f, 1.0f));
        }
    }

    private void PlaySe(const string &in path, float volume, float pitch)
    {
        PlayParams params;
        params.bus = AudioBus::SE;
        params.volume = volume;
        params.pitch = pitch;
        Audio::PlayOneShot(path, params);
    }
}

// 距離に応じた称号。目標距離に対する比で決める
string RankName(float ratio)
{
    if (ratio >= 1.4f) { return "でんせつのサル"; }
    if (ratio >= 1.0f) { return "トロッコのたつじん"; }
    if (ratio >= 0.8f) { return "あと いっぽ"; }
    if (ratio >= 0.6f) { return "ジャングルのあんないやく"; }
    if (ratio >= 0.4f) { return "かけだしのつなぎて"; }
    if (ratio >= 0.2f) { return "みならいレールこう"; }
    return "はじめのいっぽ";
}
