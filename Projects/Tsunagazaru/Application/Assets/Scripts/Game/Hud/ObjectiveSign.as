// 目標看板のテクスチャ（ポーズメニューと同じ版下）
const string kSignPlankMid = "Application/Assets/Textures/UI/Board/plank_mid.png";
const string kSignPlankCapL = "Application/Assets/Textures/UI/Board/plank_cap_l.png";
const string kSignPlankCapR = "Application/Assets/Textures/UI/Board/plank_cap_r.png";
const string kSignVineV = "Application/Assets/Textures/UI/Plant/vine_v.png";
const string kSignVineH = "Application/Assets/Textures/UI/Plant/vine_h.png";
const string kSignFoliage = "Application/Assets/Textures/UI/Plant/foliage.png";

// 横に並べた板 1 段（中板を横へ伸ばし、両端に端木を付ける）
class SignPlank
{
    UIImage@ mid;
    UIImage@ capLeft;
    UIImage@ capRight;
}

// 開幕に天井から蔦で吊り下ろす「もくひょう ○○ｍ」の看板と、締めの「つなげ！！」。
// 看板は降りて弾み、しばらくそよいでから巻き上がって消える。時間はゲーム内の時間で数える
class ObjectiveSign
{
    int sortOrder = 1800;
    float boardWidth = 720.0f;
    float boardTop = 132.0f;
    float dropSeconds = 0.50f;
    float holdSeconds = 1.15f;
    float riseSeconds = 0.30f;
    // 板と蔦の明るさ（元の値。露出は打ち消して描く）
    float brightness = 1.0f;
    Vector4 labelColor = Vector4(222.0f / 255.0f, 210.0f / 255.0f, 176.0f / 255.0f, 1.0f);
    Vector4 numberColor = Vector4(230.0f / 255.0f, 196.0f / 255.0f, 62.0f / 255.0f, 1.0f);
    Vector4 callColor = Vector4(232.0f / 255.0f, 210.0f / 255.0f, 90.0f / 255.0f, 1.0f);
    float callFontSize = 120.0f;
    float callY = 640.0f;
    float callSeconds = 1.60f;
    string font = "x8y12pxDenkiChip.ttf";

    private array<UIImage@> ropes_;
    private array<UIImage@> creepers_;
    private array<UIImage@> foliage_;
    private SignPlank@ upper_;
    private SignPlank@ lower_;
    private UIText@ label_;
    private UIText@ number_;
    private UIText@ call_;
    private bool showing_ = false;
    private float phaseTimer_ = 0.0f;
    private float swayTimer_ = 0.0f;
    private float callTimer_ = -1.0f;

    void Build(GameObject@ owner)
    {
        for (int i = 0; i < 2; ++i) {
            ropes_.insertLast(SpawnPart(owner, kSignVineV, "ObjectiveRope" + i, sortOrder));
        }
        for (int i = 0; i < 6; ++i) {
            UIImage@ creeper = SpawnPart(owner, kSignVineH, "ObjectiveCreeper" + i, sortOrder + 4);
            creeper.size = Vector2(128.0f, 48.0f);
            creepers_.insertLast(creeper);
        }
        for (int i = 0; i < 2; ++i) {
            UIImage@ leaf = SpawnPart(owner, kSignFoliage, "ObjectiveFoliage" + i, sortOrder + 5);
            leaf.size = Vector2(208.0f, 128.0f);
            foliage_.insertLast(leaf);
        }
        @upper_ = SpawnPlank(owner, "ObjectiveBoardUpper", sortOrder + 1);
        @lower_ = SpawnPlank(owner, "ObjectiveBoardLower", sortOrder + 1);

        @label_ = SpawnLabel(owner, "もくひょう", "ObjectiveLabel", 48.0f, sortOrder + 3);
        @number_ = SpawnLabel(owner, "0ｍ", "ObjectiveNumber", 84.0f, sortOrder + 3);
        @call_ = SpawnLabel(owner, "つなげ！！", "ObjectiveStartCall", callFontSize, sortOrder + 6);
    }

    // 看板を頭から降ろす
    void Show(int meters)
    {
        if (number_ is null) {
            return;
        }
        number_.text = meters + "ｍ";
        showing_ = true;
        phaseTimer_ = 0.0f;
        SetSignActive(true);
        ApplySignLayout();
    }

    // 「つなげ！！」を叩き込む
    void PlayStartCall()
    {
        if (call_ is null) {
            return;
        }
        callTimer_ = 0.0f;
        call_.gameObject.active = true;
        ApplyStartCall(0.0f);
    }

    void Tick(float deltaTime)
    {
        if (number_ is null) {
            return;
        }
        swayTimer_ += deltaTime;
        if (showing_) {
            phaseTimer_ += deltaTime;
            const float total = Max(0.01f, dropSeconds) + Max(0.0f, holdSeconds) + Max(0.01f, riseSeconds);
            if (phaseTimer_ >= total) {
                showing_ = false;
                SetSignActive(false);
            } else {
                ApplySignLayout();
            }
        }
        if (callTimer_ >= 0.0f) {
            ApplyStartCall(deltaTime);
        }
    }

    private UIImage@ SpawnPart(GameObject@ owner, const string &in texture, const string &in name, int order)
    {
        UIImage@ image = owner.SpawnUIImage(texture, name);
        image.anchor = UIAnchor::TopCenter;
        image.pivot = Vector2(0.5f, 0.5f);
        image.sortOrder = order;
        image.gameObject.active = false;
        return image;
    }

    private SignPlank@ SpawnPlank(GameObject@ owner, const string &in name, int order)
    {
        SignPlank plank;
        @plank.mid = SpawnPart(owner, kSignPlankMid, name + "Mid", order);
        @plank.capLeft = SpawnPart(owner, kSignPlankCapL, name + "CapL", order + 1);
        @plank.capRight = SpawnPart(owner, kSignPlankCapR, name + "CapR", order + 1);
        return plank;
    }

    private UIText@ SpawnLabel(GameObject@ owner, const string &in text, const string &in name, float fontSize, int order)
    {
        UIText@ label = owner.SpawnUIText(font, text, name);
        label.anchor = UIAnchor::TopCenter;
        label.pivot = Vector2(0.5f, 0.5f);
        label.fontSize = fontSize;
        label.SetAlign(TextAlignH::Center, TextAlignV::Middle);
        label.SetOutline(Vector4(0.0f, 0.0f, 0.0f, 1.0f), 0.05f);
        label.sortOrder = order;
        label.gameObject.active = false;
        return label;
    }

    // 降りる → 留まる → 巻き上がる の進み具合（1 = 定位置）
    private float DropProgress()
    {
        const float drop = Max(0.01f, dropSeconds);
        const float hold = Max(0.0f, holdSeconds);
        const float rise = Max(0.01f, riseSeconds);
        if (phaseTimer_ < drop) {
            return Ease(EaseType::EaseOutBack, phaseTimer_ / drop);
        }
        if (phaseTimer_ < drop + hold) {
            return 1.0f;
        }
        const float t = Clamp((phaseTimer_ - drop - hold) / rise, 0.0f, 1.0f);
        return 1.0f - Ease(EaseType::EaseInBack, t);
    }

    // 着地で弾む揺れと、吊られたままのそよぎ
    private float SwayAngle()
    {
        const float since = phaseTimer_ - Max(0.01f, dropSeconds);
        const float impact = since > 0.0f ? exp(-3.0f * since) * sin(since * 11.0f) * 0.055f : 0.0f;
        return impact + sin(swayTimer_ * 1.4f) * 0.012f;
    }

    private void PlacePlank(SignPlank@ plank, const Vector2 &in center, float width, float angle, const Vector4 &in color)
    {
        const float capWidth = 40.0f;
        const float capOffset = (width - capWidth) * 0.5f;
        PlacePlankPart(plank.mid, center, 0.0f, Max(0.0f, width - capWidth * 2.0f), angle, color);
        PlacePlankPart(plank.capLeft, center, -capOffset, capWidth, angle, color);
        PlacePlankPart(plank.capRight, center, capOffset, capWidth, angle, color);
    }

    private void PlacePlankPart(UIImage@ image, const Vector2 &in center, float offsetX, float sizeX, float angle, const Vector4 &in color)
    {
        const Vector2 offset = RotateOffset(Vector2(offsetX, 0.0f), angle);
        image.anchoredPosition = Vector2(center.x + offset.x, center.y + offset.y);
        image.size = Vector2(sizeX, 96.0f);
        image.rotation = angle;
        image.color = color;
    }

    private void ApplySignLayout()
    {
        const Vector4 wood = OriginalTextureTint(kTexMeanPlank, Vector4(brightness, brightness, brightness, 1.0f));
        const Vector4 plant = OriginalTextureTint(kTexMeanPlant, Vector4(brightness, brightness, brightness, 1.0f));
        const float width = Max(80.0f, boardWidth);
        const float top = boardTop - (1.0f - DropProgress()) * 900.0f;
        const float angle = SwayAngle();
        const Vector2 hinge = Vector2(0.0f, top);

        // 天井から下ろす蔦。板が降りるほど伸びる
        const float ropeLength = Max(0.0f, top);
        for (uint i = 0; i < ropes_.length(); ++i) {
            const float x = (i == 0 ? -1.0f : 1.0f) * width * 0.33f;
            ropes_[i].size = Vector2(32.0f, ropeLength);
            ropes_[i].anchoredPosition = Vector2(x, ropeLength * 0.5f);
            ropes_[i].rotation = 0.0f;
            ropes_[i].color = plant;
        }

        const Vector2 upperOffset = RotateOffset(Vector2(0.0f, 48.0f), angle);
        const Vector2 lowerOffset = RotateOffset(Vector2(0.0f, 144.0f), angle);
        PlacePlank(upper_, Vector2(hinge.x + upperOffset.x, hinge.y + upperOffset.y), width, angle, wood);
        PlacePlank(lower_, Vector2(hinge.x + lowerOffset.x, hinge.y + lowerOffset.y), width, angle, wood);

        // 板の上を這う蔦
        const float pitch = width / float(creepers_.length());
        for (uint i = 0; i < creepers_.length(); ++i) {
            const float x = -width * 0.5f + pitch * (float(i) + 0.5f);
            const Vector2 offset = RotateOffset(Vector2(x, 0.0f), angle);
            creepers_[i].anchoredPosition = Vector2(hinge.x + offset.x, hinge.y + offset.y);
            creepers_[i].rotation = angle;
            creepers_[i].color = plant;
        }

        // 肩の茂み
        for (uint i = 0; i < foliage_.length(); ++i) {
            const float side = i == 0 ? -1.0f : 1.0f;
            const Vector2 offset = RotateOffset(Vector2(side * (width * 0.5f - 24.0f), 6.0f), angle);
            foliage_[i].anchoredPosition = Vector2(hinge.x + offset.x, hinge.y + offset.y);
            foliage_[i].rotation = angle + side * 0.12f;
            foliage_[i].color = plant;
        }

        const Vector2 labelOffset = RotateOffset(Vector2(0.0f, 48.0f), angle);
        label_.anchoredPosition = Vector2(hinge.x + labelOffset.x, hinge.y + labelOffset.y);
        label_.rotation = angle;
        label_.color = labelColor;
        const Vector2 numberOffset = RotateOffset(Vector2(0.0f, 144.0f), angle);
        number_.anchoredPosition = Vector2(hinge.x + numberOffset.x, hinge.y + numberOffset.y);
        number_.rotation = angle;
        number_.color = numberColor;
    }

    // 大きく出て行き過ぎてから収まり、最後に薄れて消える
    private void ApplyStartCall(float deltaTime)
    {
        callTimer_ += deltaTime;
        const float total = Max(0.3f, callSeconds);
        if (callTimer_ >= total) {
            callTimer_ = -1.0f;
            call_.gameObject.active = false;
            return;
        }
        const float punch = Clamp(callTimer_ / 0.26f, 0.0f, 1.0f);
        const float scale = 2.0f + (1.0f - 2.0f) * Ease(EaseType::EaseOutBack, punch);
        const float fadeStart = total - 0.40f;
        const float alpha = callTimer_ <= fadeStart ? 1.0f : Clamp(1.0f - (callTimer_ - fadeStart) / 0.40f, 0.0f, 1.0f);

        call_.fontSize = callFontSize * scale;
        call_.anchoredPosition = Vector2(0.0f, callY);
        call_.rotation = -0.05f;
        call_.color = Vector4(callColor.x, callColor.y, callColor.z, alpha);
        // 本体と縁取りのアルファを揃える（片方だけ薄めると輪郭だけが残る）
        call_.SetOutline(Vector4(0.0f, 0.0f, 0.0f, alpha), 0.05f);
    }

    private void SetSignActive(bool active)
    {
        array<SignPlank@> planks = { upper_, lower_ };
        for (uint i = 0; i < planks.length(); ++i) {
            planks[i].mid.gameObject.active = active;
            planks[i].capLeft.gameObject.active = active;
            planks[i].capRight.gameObject.active = active;
        }
        for (uint i = 0; i < ropes_.length(); ++i) {
            ropes_[i].gameObject.active = active;
        }
        for (uint i = 0; i < creepers_.length(); ++i) {
            creepers_[i].gameObject.active = active;
        }
        for (uint i = 0; i < foliage_.length(); ++i) {
            foliage_[i].gameObject.active = active;
        }
        label_.gameObject.active = active;
        number_.gameObject.active = active;
    }
}

// 中心からのずれを傾きに合わせて回す
Vector2 RotateOffset(const Vector2 &in offset, float angle)
{
    const float c = cos(angle);
    const float s = sin(angle);
    return Vector2(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
}
