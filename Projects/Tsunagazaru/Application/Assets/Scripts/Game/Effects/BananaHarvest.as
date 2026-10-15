// 飛んでいる（あるいは掲げられている）バナナ 1 本
class HarvestBanana
{
    Transform@ carriage;     // 掲げる先のサルが乗っている車両
    PooledModel@ model;      // 3D で描いている間だけ借りる
    Vector3 origin;          // もぎ取られた実の位置
    float delay = 0.0f;      // 同じ瞬間に複数本出たときのずらし
    float elapsed = 0.0f;
    float sizeRate = 1.0f;   // 回復量に応じた大きさの倍率
    float swayPhase = 0.0f;
    float spin = 0.0f;       // 積み上がった自転角
    float amount = 0.0f;     // この 1 本で回復した量
    bool handoffDone = false;
    bool handoffValid = false;
    Vector2 hudStart;        // 2D へ受け渡した瞬間の画面位置
    float hudStartHeight = 0.0f;
    float hudStartAngle = 0.0f;
}

// 1 本ぶんのこのフレームの見た目
class HarvestPose
{
    Vector3 position;
    Vector3 rotation;
    Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
    float flash = 0.0f;
}

// バナナの収穫演出。サル 1 匹が木 1 本の隣を通るたびに、バナナが 1 本もぎ取られてそのサルの頭上へ飛ぶ。
// もぎ取り → 飛行 → 着地 → 掲げ → 打ち上げ のあと、一番速く上がっている瞬間に同じ画面位置・同じ大きさの
// 2D のバナナ（ゲージの粒と同じ絵）へ差し替えてスタミナゲージへ飛ばし、着いたらゲージを弾ませる。
// 大きさは回復量の割合に比例させる（下限あり）。ゲームの状態は読むだけで書き換えない
[DisplayName("バナナの収穫演出")]
class BananaHarvest : ScriptComponent
{
    [DisplayName("演出を出す")]
    bool effectEnabled = true;

    [DisplayName("バナナの大きさ"), Range(0.05, 2)]
    float sizeRate = 0.9f;

    [DisplayName("回復量が減ったときの最小倍率"), Range(0.1, 1)]
    float minSizeRate = 0.6f;

    [DisplayName("もぎ取り（秒）"), Range(0.01, 1)]
    float popDuration = 0.14f;

    [DisplayName("頭上まで飛ぶ（秒）"), Range(0.05, 2)]
    float flyDuration = 0.46f;

    [DisplayName("着地の潰れ（秒）"), Range(0.01, 1)]
    float landDuration = 0.16f;

    [DisplayName("掲げる（秒）"), Range(0, 5)]
    float holdDuration = 0.35f;

    [DisplayName("頭上から離れる（秒）"), Range(0.01, 2)]
    float tuckDuration = 0.26f;

    [DisplayName("同時に取れたときのずらし（秒）"), Range(0, 0.5)]
    float stagger = 0.08f;

    [DisplayName("放物線の高さ（マス）"), Range(0, 3)]
    float arcHeight = 0.85f;

    [DisplayName("飛行中の自転の速さ"), Range(0, 40)]
    float flySpin = 9.0f;

    [DisplayName("飛行中の伸び縮み"), Range(0, 0.8)]
    float flyStretch = 0.22f;

    [DisplayName("着地の潰れの深さ"), Range(0, 0.9)]
    float landSquash = 0.34f;

    [DisplayName("掲げている間の上下の幅（マス）"), Range(0, 0.5)]
    float holdBobHeight = 0.055f;

    [DisplayName("掲げている間の上下の速さ"), Range(0, 30)]
    float holdBobSpeed = 7.0f;

    [DisplayName("掲げている間の自転の速さ"), Range(0, 20)]
    float holdSpin = 2.4f;

    [DisplayName("掲げている間の傾きの幅"), Range(0, 1)]
    float holdSwing = 0.16f;

    [DisplayName("離れる前に持ち上げる高さ（マス）"), Range(0, 1)]
    float tuckLift = 0.14f;

    [DisplayName("離れるときの自転の速さ"), Range(0, 40)]
    float tuckSpin = 14.0f;

    [DisplayName("ゲージへ飛ばす")]
    bool hudEnabled = true;

    [DisplayName("ゲージへ入るまで（秒）"), Range(0.05, 3)]
    float hudFlyDuration = 0.5f;

    [DisplayName("ゲージへの軌道の膨らみ（px）"), Range(0, 800)]
    float hudArc = 170.0f;

    [DisplayName("頭上から打ち上がる高さ（マス）"), Range(0, 3)]
    float hudLaunchRise = 0.45f;

    [DisplayName("2D になった直後の膨らみ"), Range(0, 1.5)]
    float hudHandoffPunch = 0.3f;

    [DisplayName("ゲージへ向かう間の首振りの回数"), Range(0, 10)]
    float hudSpin = 2.2f;

    [DisplayName("2D のバナナの明るさ（元の値）"), Range(0.05, 2)]
    float hudBrightness = 0.6f;

    [DisplayName("ゲージへ入る直前の光り"), Range(0, 3)]
    float hudArriveFlash = 0.8f;

    [DisplayName("もぎ取りと着地の光り"), Range(0, 3)]
    float flashStrength = 0.7f;

    [DisplayName("効果音を鳴らす")]
    bool seEnabled = true;

    [DisplayName("効果音の音量"), Range(0, 1)]
    float seVolume = 0.35f;

    [DisplayName("効果音の基準の高さ"), Range(0.25, 4)]
    float seBasePitch = 1.2f;

    [DisplayName("サル 1 匹ごとに上げる高さ"), Range(0, 0.5)]
    float sePitchStep = 0.06f;

    [DisplayName("高さを上げ続ける段数"), Range(1, 48)]
    int sePitchSteps = 12;

    private Hunger@ hunger_;
    private TrainMovement@ train_;
    private MapView@ mapView_;
    private StaminaGauge@ gauge_;
    private ModelPool@ pool_;
    private array<HarvestBanana@> bananas_;
    private array<UIImage@> hudImages_;
    private float gridSize_ = 1.0f;

    void Setup(Hunger@ hunger, TrainMovement@ train, MapView@ mapView, StaminaGauge@ gauge)
    {
        @hunger_ = hunger;
        @train_ = train;
        @mapView_ = mapView;
        @gauge_ = gauge;
        gridSize_ = Max(train.gridSize, 0.01f);
        @pool_ = ModelPool(owner, "Application/Assets/Prefabs/Game/Banana.prefab", "BananaHarvest_");
        pool_.Prewarm(8);
        for (int i = 0; i < 24; ++i) {
            UIImage@ image = owner.SpawnUIImage(kStaminaPip, "BananaHarvestHud_" + i);
            image.anchor = UIAnchor::TopLeft;
            image.pivot = Vector2(0.5f, 0.5f);
            image.sortOrder = 960;
            image.gameObject.active = false;
            hudImages_.insertLast(image);
        }
        hunger_.SetBananaHarvestCallback(BananaHarvestCallback(this.OnHarvested));
    }

    // 車両の位置が決まった後に呼ぶ（頭上のバナナを 1 フレーム遅らせない）
    void Tick(float deltaTime)
    {
        if (pool_ is null) {
            return;
        }
        const float step = Max(deltaTime, 0.0f);
        for (uint i = 0; i < bananas_.length(); ++i) {
            bananas_[i].elapsed += step;
        }

        // 寿命が尽きたものは、消す前にゲージを弾ませる
        for (int i = int(bananas_.length()) - 1; i >= 0; --i) {
            HarvestBanana@ banana = bananas_[i];
            if (banana.elapsed - banana.delay < HudEnd()) {
                continue;
            }
            if (gauge_ !is null) {
                gauge_.PlayGainPop(banana.amount);
            }
            ReleaseModel(banana);
            bananas_.removeAt(i);
        }

        uint hudCount = 0;
        GameCamera@ camera = Scene::GetGameCamera();
        for (uint i = 0; i < bananas_.length(); ++i) {
            HarvestBanana@ banana = bananas_[i];
            const float time = banana.elapsed - banana.delay;
            if (time < ExitEnd()) {
                if (time < 0.0f) {
                    continue;
                }
                if (banana.model is null) {
                    @banana.model = pool_.Acquire();
                }
                if (banana.model !is null) {
                    HarvestPose@ pose = EvaluatePose(banana, step);
                    banana.model.Place(pose.position, pose.rotation, pose.scale);
                    banana.model.SetColor(pose.flash > 0.0f
                        ? Vector4(1.0f + pose.flash, 1.0f + pose.flash * 0.85f, 1.0f + pose.flash * 0.4f, 1.0f)
                        : Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                }
                continue;
            }

            // ここから先は 2D
            ReleaseModel(banana);
            if (!banana.handoffDone) {
                CaptureHandoff(banana, camera);
            }
            if (banana.handoffValid && hudCount < hudImages_.length()) {
                DrawHudBanana(banana, hudImages_[hudCount]);
                ++hudCount;
            }
        }
        for (uint i = hudCount; i < hudImages_.length(); ++i) {
            if (hudImages_[i].gameObject.active) {
                hudImages_[i].gameObject.active = false;
            }
        }
    }

    // ---------------------------------------------------------------- 段階の境目

    private float PopEnd() const { return Max(popDuration, 0.01f); }
    private float FlyEnd() const { return PopEnd() + Max(flyDuration, 0.01f); }
    private float LandEnd() const { return FlyEnd() + Max(landDuration, 0.01f); }
    private float HoldEnd() const { return LandEnd() + Max(holdDuration, 0.0f); }
    private float ExitEnd() const { return HoldEnd() + Max(tuckDuration, 0.01f); }
    private float HudEnd() const { return ExitEnd() + (hudEnabled ? Max(hudFlyDuration, 0.01f) : 0.0f); }

    // ---------------------------------------------------------------- 収穫の知らせ

    private void OnHarvested(BananaHarvestEvent@ harvest)
    {
        if (!effectEnabled || train_ is null) {
            return;
        }
        Transform@ carriage = train_.GetMonkeyTransform(harvest.monkeyIndex);
        if (carriage is null) {
            return;
        }
        const float modelScale = BlockLayout::GetScale(gridSize_);
        const float surfaceHeight = BlockLayout::GetSurfaceHeight(gridSize_);
        // 幹の中心ではなく、サルに近い側の房から取れたように見せる
        const float towardX = float(harvest.monkeyGridX - harvest.treeGridX);
        const float towardZ = float(harvest.monkeyGridZ - harvest.treeGridZ);

        HarvestBanana banana;
        @banana.carriage = carriage;
        banana.origin = Vector3((float(harvest.treeGridX) + towardX * 0.3f) * gridSize_,
            surfaceHeight + 1.15f * modelScale,
            (float(harvest.treeGridZ) + towardZ * 0.3f) * gridSize_);
        banana.delay = float(harvest.indexInFrame) * Max(stagger, 0.0f);
        banana.swayPhase = HashCell01(harvest.treeGridX + harvest.monkeyIndex, harvest.treeGridZ) * 2.0f * kPi;
        banana.spin = banana.swayPhase;
        banana.sizeRate = Max(Clamp(harvest.amountRate, 0.0f, 1.0f), Clamp(minSizeRate, 0.0f, 1.0f));
        banana.amount = harvest.amount;
        bananas_.insertLast(banana);

        if (mapView_ !is null) {
            mapView_.PlayBananaTreeShake(harvest.treeGridX, harvest.treeGridZ, towardX, towardZ);
        }
        if (seEnabled) {
            // 列車の後ろのサルほど高く鳴らして連鎖に聞かせる
            const int step = harvest.monkeyIndex % Max(sePitchSteps, 1);
            PlayParams params;
            params.bus = AudioBus::SE;
            params.volume = seVolume;
            params.pitch = Max(seBasePitch + float(step) * sePitchStep, 0.25f);
            Audio::PlayOneShot("Application/Assets/Sounds/SE/decision.mp3", params);
        }
    }

    private void ReleaseModel(HarvestBanana@ banana)
    {
        if (banana.model !is null) {
            pool_.Release(banana.model);
            @banana.model = null;
        }
    }

    // ---------------------------------------------------------------- 3D

    // サルの頭のてっぺん（車両の拡大を掛けるので、連結直後の出現演出で潰れている間も浮かない）
    private Vector3 HeadPosition(Transform@ carriage) const
    {
        const Vector3 base = carriage.worldPosition;
        return Vector3(base.x, base.y + 1.45f * carriage.scale.y, base.z);
    }

    private HarvestPose@ EvaluatePose(HarvestBanana@ banana, float deltaTime)
    {
        HarvestPose pose;
        const float time = banana.elapsed - banana.delay;
        const float baseScale = BlockLayout::GetScale(gridSize_) * Max(sizeRate, 0.01f) * banana.sizeRate;
        const Vector3 head = HeadPosition(banana.carriage);
        Vector3 stretch = Vector3(1.0f, 1.0f, 1.0f);
        float sizeFade = 1.0f;
        float swing = 0.0f;

        if (time < PopEnd()) {
            // もぎ取り。実の位置で 0 から膨らみ、行き過ぎて戻る
            const float progress = Normalize01(time, PopEnd());
            sizeFade = Ease(EaseType::EaseOutBack, progress);
            pose.position = banana.origin;
            pose.position.y += Ease(EaseType::EaseOutQuad, progress) * 0.16f * gridSize_;
            banana.spin += flySpin * 1.6f * deltaTime;
            swing = sin(progress * kPi) * 0.55f;
            pose.flash = flashStrength * (1.0f - progress);
        } else if (time < FlyEnd()) {
            // 飛行。放物線でサルの頭上へ
            const float progress = Normalize01(time - PopEnd(), FlyEnd() - PopEnd());
            const float horizontal = Ease(EaseType::EaseOutQuad, progress);
            const Vector3 from = Vector3(banana.origin.x, banana.origin.y + 0.16f * gridSize_, banana.origin.z);
            pose.position = from + (head - from) * horizontal;
            pose.position.y += sin(progress * kPi) * arcHeight * gridSize_;
            banana.spin += flySpin * deltaTime;
            swing = cos(progress * kPi) * 0.45f;
            stretch = StretchFactor(1.0f + cos(progress * kPi) * flyStretch);
        } else if (time < LandEnd()) {
            // 着地。頭に着いた瞬間に潰れ、跳ね返って収まる
            const float progress = Normalize01(time - FlyEnd(), LandEnd() - FlyEnd());
            const float recoil = sin(progress * 2.0f * kPi) * (1.0f - progress);
            pose.position = head;
            stretch = StretchFactor(1.0f - recoil * landSquash);
            banana.spin += Lerp(flySpin, holdSpin, progress) * deltaTime;
            swing = 0.45f * (1.0f - progress);
            pose.flash = flashStrength * 0.8f * (1.0f - progress);
        } else if (time < HoldEnd()) {
            // 掲げ。止まって見えないよう、上下・首振り・自転を止めない
            const float sway = time * holdBobSpeed + banana.swayPhase;
            pose.position = head;
            pose.position.y += sin(sway) * holdBobHeight * gridSize_;
            stretch = StretchFactor(1.0f + sin(sway) * 0.06f);
            banana.spin += holdSpin * deltaTime;
            swing = sin(sway * 0.5f) * holdSwing;
        } else if (hudEnabled) {
            // 打ち上げ。いったん頭へ沈み込んでから、終わりで一番速くなるよう跳ね上がる
            const float progress = Normalize01(time - HoldEnd(), ExitEnd() - HoldEnd());
            const float crouch = Normalize01(progress, 0.35f);
            const float rise = Normalize01(progress - 0.35f, 0.65f);
            pose.position = head;
            pose.position.y += -sin(crouch * kPi) * tuckLift * gridSize_
                + Ease(EaseType::EaseInQuad, rise) * hudLaunchRise * gridSize_;
            stretch = StretchFactor(1.0f - sin(crouch * kPi) * 0.22f + rise * 0.25f);
            banana.spin += Lerp(holdSpin, tuckSpin, rise) * deltaTime;
            swing = sin(rise * kPi) * holdSwing * 1.5f;
        } else {
            // 収納。一度持ち上げてから、縮みながら頭へ吸い込ませる
            const float progress = Normalize01(time - HoldEnd(), ExitEnd() - HoldEnd());
            const float lift = Normalize01(progress, 0.3f);
            const float sink = Normalize01(progress - 0.3f, 0.7f);
            pose.position = head;
            pose.position.y += Ease(EaseType::EaseOutBack, lift) * tuckLift * gridSize_
                - Ease(EaseType::EaseInCubic, sink) * (tuckLift + 0.2f) * gridSize_;
            sizeFade = Max(1.0f - Ease(EaseType::EaseInBack, sink), 0.0f);
            banana.spin += Lerp(holdSpin, tuckSpin, sink) * deltaTime;
            swing = sin(sink * kPi) * holdSwing * 1.5f;
        }

        const float scale = baseScale * sizeFade;
        pose.scale = Vector3(scale * stretch.x, scale * stretch.y, scale * stretch.z);
        // 原点が房の根元にあるモデルなので、大きく倒すのは Z（首振り）だけ
        pose.rotation = Vector3(0.0f, banana.spin, swing);
        return pose;
    }

    // ---------------------------------------------------------------- 2D

    // 打ち上げの終わりに 3D のバナナがいた場所の根元と先端を射影し、画面位置・高さ・傾きを 1 度だけ測る
    private void CaptureHandoff(HarvestBanana@ banana, GameCamera@ camera)
    {
        banana.handoffDone = true;
        banana.handoffValid = false;
        if (!hudEnabled || camera is null || gauge_ is null || banana.carriage is null) {
            return;
        }
        const float baseScale = BlockLayout::GetScale(gridSize_) * Max(sizeRate, 0.01f) * banana.sizeRate;
        const Vector3 head = HeadPosition(banana.carriage);
        const float launchY = head.y + hudLaunchRise * gridSize_;
        const Vector3 root = camera.WorldToScreenPoint(Vector3(head.x, launchY + 0.2f * baseScale, head.z));
        const Vector3 tip = camera.WorldToScreenPoint(Vector3(head.x, launchY + 1.3f * baseScale, head.z));
        // カメラの後ろは測れないので 2D へは出さない（ゲージだけ弾ませる）
        if (root.z <= 1.0e-5f || tip.z <= 1.0e-5f) {
            return;
        }
        const float dx = tip.x - root.x;
        const float dy = tip.y - root.y;
        const float height = sqrt(dx * dx + dy * dy);
        if (height < 1.0f) {
            return;
        }
        banana.handoffValid = true;
        banana.hudStart = Vector2((root.x + tip.x) * 0.5f, (root.y + tip.y) * 0.5f);
        banana.hudStartHeight = height;
        banana.hudStartAngle = atan2(dx, -dy);
    }

    private void DrawHudBanana(HarvestBanana@ banana, UIImage@ image)
    {
        Vector2 target;
        Vector2 pipSize;
        if (!gauge_.TryGetFillFrontTarget(target, pipSize)) {
            if (image.gameObject.active) {
                image.gameObject.active = false;
            }
            return;
        }
        const float time = banana.elapsed - banana.delay;
        const float progress = Normalize01(time - ExitEnd(), HudEnd() - ExitEnd());

        // いったん上へ膨らませてからゲージへ落とす
        const Vector2 control = Vector2(Lerp(banana.hudStart.x, target.x, 0.3f), Lerp(banana.hudStart.y, target.y, 0.3f) - hudArc);
        const float travel = Ease(EaseType::EaseInOutSine, progress);
        const float inv = 1.0f - travel;
        const Vector2 position = Vector2(
            banana.hudStart.x * inv * inv + control.x * 2.0f * inv * travel + target.x * travel * travel,
            banana.hudStart.y * inv * inv + control.y * 2.0f * inv * travel + target.y * travel * travel);

        // 受け渡しの瞬間は 3D の実測の大きさで、そこから粒の大きさへ寄せる。直後だけ膨らませる
        const float shrink = Ease(EaseType::EaseInQuad, progress);
        const float height = Lerp(banana.hudStartHeight, pipSize.y, shrink);
        const float punch = 1.0f + hudHandoffPunch * sin(Normalize01(progress, 0.3f) * kPi);
        const float aspect = pipSize.y > 0.0f ? pipSize.x / pipSize.y : 0.33f;
        image.size = Vector2(height * aspect * punch, height * punch);

        // 受け渡し時の傾きから粒と同じ真っ直ぐへ収め、途中は減衰する首振りを乗せる
        const float settle = Ease(EaseType::EaseOutCubic, progress);
        const float wobble = sin(progress * hudSpin * 2.0f * kPi) * 0.35f * (1.0f - progress);
        image.rotation = banana.hudStartAngle * (1.0f - settle) + wobble;

        // ゲージへ入る直前だけ光らせる
        const float arrive = Normalize01(progress - 0.75f, 0.25f);
        const float brightness = hudBrightness * (1.0f + arrive * arrive * hudArriveFlash);
        image.anchoredPosition = position;
        image.color = Vector4(OriginalHudChannel(brightness), OriginalHudChannel(brightness), OriginalHudChannel(brightness * 0.95f), 1.0f);
        if (!image.gameObject.active) {
            image.gameObject.active = true;
        }
    }
}

// 0..1 へ正規化する（長さ 0 なら 1）
float Normalize01(float value, float length)
{
    return length > 0.0f ? Clamp(value / length, 0.0f, 1.0f) : 1.0f;
}

// 体積を保ったまま縦へ伸び縮みさせる係数
Vector3 StretchFactor(float stretch)
{
    const float safe = Max(stretch, 0.05f);
    const float width = 1.0f / sqrt(safe);
    return Vector3(width, safe, width);
}
