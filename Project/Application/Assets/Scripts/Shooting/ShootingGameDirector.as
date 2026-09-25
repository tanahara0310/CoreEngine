// ゲームの進行：カウントダウン → 敵を出す → 時間切れでクリア／自機が落ちたらゲームオーバー → リザルトへ
enum ShootingPhase
{
    Countdown,
    Playing,
    Finished
}

[DisplayName("シューティング：進行")]
class ShootingGameDirector : ScriptComponent
{
    [ObjectRef] [Tooltip("自機")]
    GameObject@ player;

    [ObjectRef] [Tooltip("スコアの文字")]
    GameObject@ scoreText;

    [ObjectRef] [Tooltip("残り時間の文字")]
    GameObject@ timeText;

    [ObjectRef] [Tooltip("残機の文字")]
    GameObject@ lifeText;

    [ObjectRef] [Tooltip("画面の中央に出す大きな文字（カウントダウンと結果）")]
    GameObject@ centerText;

    [ObjectRef] [Tooltip("中央の大きな文字の下に出す文字（ボーナス）")]
    GameObject@ subText;

    [Asset("Prefab")] [Tooltip("まっすぐ来る敵")]
    string dronePrefab = "Application/Assets/Prefabs/Shooting/EnemyDrone.prefab";

    [Asset("Prefab")] [Tooltip("左右に揺れながら来る敵")]
    string weaverPrefab = "Application/Assets/Prefabs/Shooting/EnemyWeaver.prefab";

    [Asset("Prefab")] [Tooltip("止まって撃つ敵")]
    string gunnerPrefab = "Application/Assets/Prefabs/Shooting/EnemyGunner.prefab";

    [Asset("Prefab")] [Tooltip("倒したときに浮かぶ点数")]
    string scorePopupPrefab = "Application/Assets/Prefabs/Shooting/ScorePopup.prefab";

    [Range(10.0f, 300.0f)] [Tooltip("ステージの長さ（秒）")]
    float stageTime = 60.0f;

    [Tooltip("始まる前のカウントダウン（秒）")]
    float countdownTime = 3.0f;

    [Tooltip("始めの敵の出る間隔（秒）")]
    float spawnIntervalStart = 1.0f;

    [Tooltip("終わり際の敵の出る間隔（秒）")]
    float spawnIntervalEnd = 0.4f;

    [Tooltip("敵を出す奥行き")]
    float spawnZ = 23.0f;

    [Tooltip("敵を出す横幅（中心から片側）")]
    float spawnHalfWidth = 14.0f;

    [Tooltip("クリアしたとき、残機 1 つあたりのボーナス")]
    int lifeBonus = 1000;

    [Tooltip("結果を出してからリザルトへ移るまでの秒数")]
    float resultDelay = 3.0f;

    [Range(0.05f, 1.0f)] [Tooltip("自機が落ちたときのスローの速さ")]
    float slowMotionScale = 0.3f;

    [Tooltip("移る先のシーン")]
    string resultScene = "ShootingResult";

    private ShootingPlayer@ player_;
    private ShootingPhase phase_ = ShootingPhase::Countdown;
    private float countdown_ = 0.0f;
    private float elapsed_ = 0.0f;
    private float spawnTimer_ = 0.0f;
    private float finishTimer_ = 0.0f;
    private int score_ = 0;
    private int kills_ = 0;
    private int lives_ = 0;
    private int bonus_ = 0;
    private bool cleared_ = false;
    private bool leaving_ = false;
    private int shownCount_ = -1;
    private int shownSeconds_ = -1;
    private float centerFade_ = 0.0f;
    private float centerFontSize_ = 120.0f;
    private Vector4 centerColor_;
    private Vector4 timeColor_;

    void Start()
    {
        // UI の選択を外し、操作をゲームの場面へ戻す
        UI::ClearFocus();
        Time::SetTimeScale(1.0f);

        if (player !is null) {
            player.GetComponent(@player_);
        }
        lives_ = player_ !is null ? player_.lives : 0;
        countdown_ = countdownTime;

        if (centerText !is null) {
            centerColor_ = centerText.uiText.color;
            centerFontSize_ = centerText.uiText.fontSize;
            centerText.uiText.text = "";
        }
        if (timeText !is null) {
            timeColor_ = timeText.uiText.color;
        }
        if (subText !is null) {
            subText.uiText.text = "";
        }
        UpdateScore();
        UpdateLives();
        UpdateTime();
        Log("シューティング: ゲーム開始（ステージ " + int(stageTime) + " 秒・残機 " + lives_ + "）");
    }

    void Update()
    {
        const float dt = Time::DeltaTime();
        if (phase_ == ShootingPhase::Countdown) {
            UpdateCountdown(dt);
        } else if (phase_ == ShootingPhase::Playing) {
            UpdatePlaying(dt);
        } else {
            UpdateFinished();
        }
        FadeCenterText(Time::UnscaledDeltaTime());
    }

    // 敵を倒して点数が入るときか（カウントダウン中と終わった後は入らない）
    bool IsPlaying() const
    {
        return phase_ == ShootingPhase::Playing;
    }

    // 自機を動かしてよいか（ゲームオーバーの後は止める）
    bool CanMove() const
    {
        return phase_ != ShootingPhase::Finished || cleared_;
    }

    // 自機の位置（敵が狙う先）
    Vector3 GetPlayerPosition()
    {
        return player !is null ? player.transform.worldPosition : Vector3(0.0f, 0.0f, -6.0f);
    }

    // 敵を倒した
    void AddKill(int points, const Vector3 &in position)
    {
        if (phase_ != ShootingPhase::Playing) {
            return;
        }
        score_ += points;
        ++kills_;
        UpdateScore();
        SpawnPopup(points, position);
    }

    // 自機が被弾した（まだ残機がある）
    void OnPlayerHit(int livesLeft)
    {
        lives_ = livesLeft;
        UpdateLives();
    }

    // 自機が落ちた
    void OnPlayerDestroyed()
    {
        lives_ = 0;
        UpdateLives();
        Finish(false);
    }

    // 中央の文字の大きさの倍率（Tween から呼ばれる）
    void SetCenterScale(float scale)
    {
        centerText.uiText.fontSize = centerFontSize_ * scale;
    }

    private void UpdateCountdown(float dt)
    {
        countdown_ -= dt;
        if (countdown_ > 0.0f) {
            const int count = int(ceil(countdown_));
            if (count != shownCount_) {
                shownCount_ = count;
                ShowCenter("" + count, 0.0f);
            }
            return;
        }
        phase_ = ShootingPhase::Playing;
        ShowCenter("GO!", 0.8f);
    }

    private void UpdatePlaying(float dt)
    {
        elapsed_ += dt;
        UpdateTime();

        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0.0f) {
            const float progress = Saturate(elapsed_ / stageTime);
            spawnTimer_ = Lerp(spawnIntervalStart, spawnIntervalEnd, progress);
            SpawnWave(progress);
        }

        if (elapsed_ >= stageTime) {
            Finish(true);
        }
    }

    private void UpdateFinished()
    {
        finishTimer_ += Time::UnscaledDeltaTime();
        // スローは少しだけ見せて戻す
        if (!cleared_ && finishTimer_ >= 1.2f && Time::TimeScale() < 1.0f) {
            Time::SetTimeScale(1.0f);
        }
        if (leaving_ || finishTimer_ < resultDelay) {
            return;
        }
        leaving_ = true;
        Time::SetTimeScale(1.0f);
        Session::SetInt("Shooting.Score", score_);
        Session::SetInt("Shooting.Kills", kills_);
        Session::SetInt("Shooting.Bonus", bonus_);
        Session::SetBool("Shooting.Cleared", cleared_);
        Scene::ChangeScene(resultScene);
    }

    private void Finish(bool cleared)
    {
        if (phase_ == ShootingPhase::Finished) {
            return;
        }
        phase_ = ShootingPhase::Finished;
        cleared_ = cleared;
        finishTimer_ = 0.0f;

        if (cleared) {
            bonus_ = lives_ * lifeBonus;
            score_ += bonus_;
            UpdateScore();
            ShowCenter("STAGE CLEAR", 0.0f);
            if (subText !is null) {
                subText.uiText.text = "残機ボーナス  +" + bonus_;
            }
            ExplodeRemainingEnemies();
        } else {
            ShowCenter("GAME OVER", 0.0f);
            Time::SetTimeScale(slowMotionScale);
        }
        Log("シューティング: " + (cleared ? "クリア" : "ゲームオーバー") + "（スコア " + score_
            + " / 撃破 " + kills_ + " / 経過 " + int(elapsed_) + " 秒）");
    }

    // 敵の出し方：時間が進むほど間隔が縮み、揺れる敵と撃つ敵が混ざる
    private void SpawnWave(float progress)
    {
        const float roll = Random::Range(0.0f, 1.0f);
        const float gunnerChance = progress < 0.3f ? 0.0f : 0.1f + 0.15f * progress;
        const float weaverChance = progress < 0.12f ? 0.0f : 0.3f;
        if (roll < gunnerChance) {
            Spawn(gunnerPrefab, RandomX(0.75f), spawnZ);
            return;
        }
        if (roll < gunnerChance + weaverChance) {
            Spawn(weaverPrefab, RandomX(0.75f), spawnZ);
            return;
        }
        if (Random::Chance(0.15f + 0.25f * progress)) {
            // 3 機が V の字に並んで来る
            const float center = RandomX(0.8f);
            Spawn(dronePrefab, center, spawnZ);
            Spawn(dronePrefab, center - 2.4f, spawnZ + 1.6f);
            Spawn(dronePrefab, center + 2.4f, spawnZ + 1.6f);
            return;
        }
        Spawn(dronePrefab, RandomX(1.0f), spawnZ);
    }

    private float RandomX(float widthRate)
    {
        const float half = spawnHalfWidth * widthRate;
        return Random::Range(-half, half);
    }

    private void Spawn(const string &in prefab, float x, float z)
    {
        GameObject@ enemy = owner.InstantiatePrefab(prefab, "Enemy");
        if (enemy is null) {
            Warn("シューティング: 敵のプレハブを読めませんでした: " + prefab);
            return;
        }
        enemy.transform.position = Vector3(x, 0.0f, z);
    }

    private void SpawnPopup(int points, const Vector3 &in position)
    {
        GameObject@ popup = owner.InstantiatePrefab(scorePopupPrefab, "ScorePopup");
        if (popup is null) {
            return;
        }
        popup.transform.position = position + Vector3(0.0f, 1.5f, 0.0f);
        popup.text3DRenderer.text = "+" + points;
    }

    // ステージが終わったとき、残っている敵をまとめて爆発させる（点数は入らない）
    private void ExplodeRemainingEnemies()
    {
        array<GameObject@>@ enemies = Physics::OverlapSphere(Vector3(0.0f, 0.0f, 5.0f), 60.0f,
            Physics::LayerMask(CollisionLayer::Enemy));
        for (uint i = 0; i < enemies.length(); ++i) {
            ShootingEnemy@ enemy;
            if (enemies[i].GetComponent(@enemy)) {
                enemy.Explode();
            }
        }
    }

    private void ShowCenter(const string &in text, float holdSeconds)
    {
        if (centerText is null) {
            return;
        }
        centerText.uiText.text = text;
        centerText.uiText.color = centerColor_;
        centerFade_ = holdSeconds;
        Tween::KillById("ShootingCenterPop");
        Tween::To(1.6f, 1.0f, 0.35f, TweenFloatSetter(this.SetCenterScale))
            .SetEase(EaseType::EaseOutBack)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("ShootingCenterPop")
            .SetLink(owner);
    }

    // 表示の時間を決めた中央の文字を、最後の 0.3 秒で消す
    private void FadeCenterText(float dt)
    {
        if (centerFade_ <= 0.0f || centerText is null) {
            return;
        }
        centerFade_ -= dt;
        Vector4 color = centerColor_;
        color.w = centerColor_.w * Saturate(centerFade_ / 0.3f);
        centerText.uiText.color = color;
        if (centerFade_ <= 0.0f) {
            centerText.uiText.text = "";
        }
    }

    private void UpdateScore()
    {
        if (scoreText !is null) {
            scoreText.uiText.text = "SCORE  " + formatInt(score_, "0", 7);
        }
    }

    private void UpdateLives()
    {
        if (lifeText is null) {
            return;
        }
        string marks = "";
        for (int i = 0; i < lives_; ++i) {
            marks += "■";
        }
        lifeText.uiText.text = "LIFE  " + marks;
    }

    private void UpdateTime()
    {
        if (timeText is null) {
            return;
        }
        const int seconds = int(ceil(Max(stageTime - elapsed_, 0.0f)));
        if (seconds == shownSeconds_) {
            return;
        }
        shownSeconds_ = seconds;
        timeText.uiText.text = "TIME  " + seconds;
        // 残り 10 秒からは赤くする
        timeText.uiText.color = seconds <= 10 ? Vector4(1.0f, 0.3f, 0.25f, timeColor_.w) : timeColor_;
    }
}
