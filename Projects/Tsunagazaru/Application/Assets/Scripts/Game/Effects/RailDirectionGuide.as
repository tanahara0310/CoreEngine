using namespace CoreEngine;

// レールの先頭の上下左右へ、床と平行な矢印を 1 つずつ置く。出すのは「そこへレールを伸ばせる向き」だけで、
// 伸ばせない向き（マップの外・空白・駅・バナナの木・既にあるレール）には何も出さない。
// 直前のマスへの入力はレールの取り消しなので、そこだけ「×」を別の色・大きさで出す。
// スタミナが足りない向きは薄く出す。開幕の演出のあいだとポーズ中は引っ込める
[DisplayName("レールの向きの案内")]
class RailDirectionGuide : ScriptComponent
{
    [DisplayName("矢印を出す")]
    bool guideEnabled = true;

    [DisplayName("矢印の大きさ（マス）"), Range(0.1, 3)]
    float fontSize = 0.6f;

    [DisplayName("先頭のマスからの距離（マス）"), Range(0, 2)]
    float offset = 1.0f;

    [DisplayName("レールの上面から浮かせる高さ（マス）"), Range(0, 2)]
    float heightOffset = 0.3f;

    [DisplayName("縁取りの色")]
    Vector4 outlineColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);

    [DisplayName("縁取りの太さ（em）"), Range(0, 0.1)]
    float outlineWidth = 0.085f;

    [DisplayName("出入りの幅（マス）"), Range(0, 0.5)]
    float pulseAmplitude = 0.08f;

    [DisplayName("出入りの速さ"), Range(0, 20)]
    float pulseSpeed = 3.5f;

    [DisplayName("伸ばせる向きの色")]
    Vector4 extendColor = Vector4(1.0f, 0.97f, 0.72f, 1.0f);

    [DisplayName("戻る向きの色")]
    Vector4 backColor = Vector4(1.0f, 0.52f, 0.42f, 1.0f);

    [DisplayName("戻る向きの大きさの倍率"), Range(0.1, 2)]
    float backSizeScale = 0.8f;

    [DisplayName("スタミナが足りない向きの不透明度"), Range(0, 1)]
    float shortageAlpha = 0.3f;

    [DisplayName("戻る向きにも出す")]
    bool showBack = true;

    [DisplayName("戻る向きも矢印で出す")]
    bool backAsArrow = false;

    private RailPath@ path_;
    private MapGenerator@ map_;
    private Hunger@ hunger_;
    private RailBuilder@ builder_;
    private PauseMenu@ pauseMenu_;
    private array<Text3DRenderer@> markers_;
    private float pulsePhase_ = 0.0f;
    private float introReveal_ = 1.0f;

    void Setup(RailPath@ path, MapGenerator@ map, Hunger@ hunger, RailBuilder@ builder, PauseMenu@ pauseMenu)
    {
        @path_ = path;
        @map_ = map;
        @hunger_ = hunger;
        @builder_ = builder;
        @pauseMenu_ = pauseMenu;
        for (int i = 0; i < 4; ++i) {
            GameObject@ marker = owner.InstantiatePrefab("Application/Assets/Prefabs/Game/RailGuideArrow.prefab", "RailDirectionGuide_" + i);
            if (marker is null) {
                break;
            }
            marker.active = false;
            markers_.insertLast(marker.text3DRenderer);
        }
    }

    // 開幕の演出から渡される登場の進み具合（0 の間は出さない・矢印は伸び上がる）
    void SetIntroReveal(float reveal)
    {
        introReveal_ = Clamp(reveal, 0.0f, 2.0f);
    }

    // カーソルが動いた後に呼ぶ
    void Tick(float deltaTime)
    {
        pulsePhase_ += Max(deltaTime, 0.0f) * pulseSpeed;
        if (pulsePhase_ >= kTau) {
            pulsePhase_ = pulsePhase_ - kTau * floor(pulsePhase_ / kTau);
        }

        const bool show = guideEnabled && path_ !is null && introReveal_ > 0.0f
            && (builder_ is null || !builder_.IsStopped())
            && (pauseMenu_ is null || !pauseMenu_.IsOpen());
        if (!show) {
            HideAll();
            return;
        }

        // 先頭は未確定のレールの末尾（無ければ確定の末尾）。取り消せるのは未確定があるときだけ
        const int pending = path_.GetPendingCount();
        const int head = pending > 0 ? path_.GetPending(pending - 1) : path_.GetLastConfirmed();
        const int previous = pending >= 2 ? path_.GetPending(pending - 2) : path_.GetLastConfirmed();
        const bool hasPrevious = pending > 0;

        const float gridSize = Max(0.01f, builder_.gridSize);
        const float height = BlockLayout::GetRailTopHeight(gridSize) + heightOffset * gridSize;
        const float distance = offset + sin(pulsePhase_) * pulseAmplitude;
        const array<int> dx = { 1, -1, 0, 0 };
        const array<int> dz = { 0, 0, 1, -1 };
        const array<string> arrows = { "→", "←", "↑", "↓" };
        for (uint i = 0; i < markers_.length(); ++i) {
            UpdateMarker(markers_[i], dx[i], dz[i], arrows[i], head, previous, hasPrevious, gridSize, height, distance);
        }
    }

    void HideAll()
    {
        for (uint i = 0; i < markers_.length(); ++i) {
            if (markers_[i].gameObject.active) {
                markers_[i].gameObject.active = false;
            }
        }
    }

    // 0 = 出さない ／ 1 = 伸ばせる ／ 2 = スタミナが足りない ／ 3 = 戻る（取り消し）
    private int Classify(int nextX, int nextZ, int head, int previous, bool hasPrevious) const
    {
        if (nextX < 0 || nextZ < 0 || nextZ >= path_.mapSizeZ) {
            return 0;
        }
        if (hasPrevious && CellKey(nextX, nextZ) == previous) {
            return 3;
        }
        if (path_.HasRail(nextX, nextZ)) {
            return 0;
        }
        if (!map_.CanConnectRail(CellX(head), CellZ(head), nextX, nextZ)) {
            return 0;
        }
        return hunger_.GetCurrent() >= builder_.GetPlacementCost(nextX, nextZ) ? 1 : 2;
    }

    private void UpdateMarker(Text3DRenderer@ marker, int dx, int dz, const string &in arrow, int head, int previous,
        bool hasPrevious, float gridSize, float height, float distance)
    {
        const int state = Classify(CellX(head) + dx, CellZ(head) + dz, head, previous, hasPrevious);
        const bool isBack = state == 3;
        if (state == 0 || (isBack && !showBack)) {
            if (marker.gameObject.active) {
                marker.gameObject.active = false;
            }
            return;
        }
        if (!marker.gameObject.active) {
            marker.gameObject.active = true;
        }
        const string text = isBack && !backAsArrow ? "×" : arrow;
        if (marker.text != text) {
            marker.text = text;
        }
        // 登場の間は同じ場所で伸び上がる
        marker.fontSize = fontSize * gridSize * introReveal_ * (isBack ? backSizeScale : 1.0f);

        Vector4 color = isBack ? backColor : extendColor;
        Vector4 outline = outlineColor;
        if (state == 2) {
            color.w *= shortageAlpha;
            outline.w *= shortageAlpha;
        }
        marker.color = color;
        marker.outlineColor = outline;
        marker.outlineWidth = outlineWidth;

        Transform@ transform = marker.gameObject.transform;
        transform.position = Vector3((float(CellX(head)) + float(dx) * distance) * gridSize, height,
            (float(CellZ(head)) + float(dz) * distance) * gridSize);
        transform.rotation = Vector3(kPi * 0.5f, 0.0f, 0.0f);
    }
}
