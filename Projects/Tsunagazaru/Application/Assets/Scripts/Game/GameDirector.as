// GameScene の司令塔。部品どうしをつなぎ、毎フレームの更新を決まった順に呼ぶ（部品は自分では更新しない）。
// 順番は元のゲームのオブジェクトの生成順と同じ：レールの表示 → カーソル → トロッコ → 投石 → マップの表示、
// LateUpdate で 進行 → カーソルの高さ
[DisplayName("GameScene の司令塔")]
class GameDirector : ScriptComponent
{
    [DisplayName("BGM")]
    string bgmPath = "Application/Assets/Sounds/BGM/Game_bgm.mp3";

    [DisplayName("BGM の音量"), Range(0, 1)]
    float bgmVolume = 1.0f / 3.0f;

    private MapGenerator@ map_;
    private MapView@ mapView_;
    private RailPath@ path_;
    private RailView@ railView_;
    private RailBuilder@ builder_;
    private TrainMovement@ train_;
    private RockThrow@ rockThrow_;
    private GameManager@ gameManager_;
    private Hunger@ hunger_;
    private StaminaGauge@ stamina_;
    private SpeedGauge@ speedGauge_;
    private GameEntrance@ entrance_;
    private PauseMenu@ pauseMenu_;
    private BananaHarvest@ harvest_;
    private Sound@ bgm_;
    private bool ready_ = false;

    void Start() override
    {
        GameResult::Reset();

        @gameManager_ = FindGameManager("GameManager");
        @hunger_ = FindHunger("GameManager");
        @map_ = FindMapGenerator("MapGenerator");
        @mapView_ = FindMapView("MapGenerator");
        @path_ = FindRailPath("RailPath");
        @railView_ = FindRailView("RailPath");
        @builder_ = FindRailBuilder("RailBuilder");
        @train_ = FindTrainMovement("Train");
        @rockThrow_ = FindRockThrow("RockProjectile");
        @stamina_ = FindStaminaGauge("StaminaGauge");
        @speedGauge_ = FindSpeedGauge("SpeedGauge");
        @entrance_ = FindGameEntrance("GameManager");
        @pauseMenu_ = FindPauseMenu("GameManager");
        @harvest_ = FindBananaHarvest("GameManager");
        if (gameManager_ is null || hunger_ is null || map_ is null || mapView_ is null || path_ is null
            || railView_ is null || builder_ is null || train_ is null || rockThrow_ is null) {
            Error("GameDirector: GameScene の部品がそろっていないので動かしません");
            return;
        }

        GameObject@ debris = owner.InstantiatePrefab("Application/Assets/Prefabs/Game/RockBreakDebris.prefab", "RockBreakDebris");
        hunger_.Setup(map_, gameManager_);
        mapView_.Setup(map_);
        railView_.Setup(path_, map_);
        train_.Setup(path_, gameManager_, hunger_);
        builder_.Setup(path_, map_, train_, hunger_, rockThrow_, debris);
        if (stamina_ !is null) {
            stamina_.Setup(hunger_, builder_);
        }
        if (speedGauge_ !is null) {
            speedGauge_.Setup(train_);
        }

        hunger_.SetMonkeyAddedCallback(MonkeyAddedCallback(this.OnMonkeyAdded));
        hunger_.SetStationPopCallback(StationCallback(this.OnStationPop));
        hunger_.SetStationEnteredCallback(StationCallback(this.OnStationEntered));
        gameManager_.SetOnEnding(GameEndingCallback(this.OnEnding));

        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        params.volume = bgmVolume;
        @bgm_ = Audio::PlayScoped(bgmPath, params);

        if (pauseMenu_ !is null) {
            pauseMenu_.Setup(gameManager_);
        }
        if (harvest_ !is null) {
            harvest_.Setup(hunger_, train_, mapView_, stamina_);
        }
        if (entrance_ !is null) {
            entrance_.Setup(builder_, train_, mapView_, stamina_, speedGauge_, pauseMenu_);
        }
        ready_ = true;
    }

    // ポーズ中は部品を進めない（元のゲームの停止と同じく、ゲームの物は全部止まる）
    void Update() override
    {
        if (!ready_ || GamePause::paused) {
            return;
        }
        const float deltaTime = Time::DeltaTime();
        if (entrance_ !is null) {
            entrance_.Tick(deltaTime);
        }

        railView_.Tick(deltaTime);
        builder_.Tick(deltaTime);
        train_.Tick(deltaTime);
        rockThrow_.Tick();
        mapView_.Tick(deltaTime);
        if (harvest_ !is null) {
            harvest_.Tick(deltaTime);
        }

        if (entrance_ !is null) {
            entrance_.TickLate(deltaTime);
        }
    }

    void LateUpdate() override
    {
        if (!ready_ || GamePause::paused) {
            return;
        }
        gameManager_.TickLate();
        builder_.TickLate();
    }

    void OnDestroy() override
    {
        @bgm_ = null;
    }

    // ---------------------------------------------------------------- 部品からの知らせ

    // 駅から連結したら、車両とサルを足す
    private void OnMonkeyAdded(int monkeyCount)
    {
        train_.SpawnCarriage(monkeyCount);
    }

    // サルが増えた駅を弾ませる
    private void OnStationPop(int stationX, int stationZ)
    {
        mapView_.PlayStationPop(stationX, stationZ);
    }

    // 駅に入って速さが落ちる瞬間に、駅・カメラ・速度計・音を同時に鳴らして理由を見せる。
    // 強さは落ちた速さに比例させ、小さすぎる落差では出さない
    private void OnStationEntered(int stationX, int stationZ)
    {
        const float dropped = train_.GetMoveSpeed() - train_.GetMinMoveSpeed();
        if (dropped <= 0.08f) {
            return;
        }
        const float strength = Max(Clamp(dropped / 3.0f, 0.0f, 1.0f), 0.35f);
        mapView_.PlayStationPop(stationX, stationZ);

        CameraShakeParams lurch;
        lurch.positionAmplitude = Vector3(0.12f * strength, 0.03f * strength, 0.0f);
        lurch.rotationAmplitude = Vector3(0.8f * strength, 0.5f * strength, 1.6f * strength);
        lurch.frequency = 5.0f;
        lurch.duration = 0.3f;
        lurch.decayEase = EaseType::EaseOutCubic;
        // カメラの右＝トロッコの進む向きへつんのめる
        lurch.direction = Vector3(1.0f, 0.0f, 0.0f);
        lurch.directionality = 0.85f;
        lurch.waveform = ShakeWaveform::Kick;
        lurch.space = ShakeSpace::CameraLocal;
        CameraShake::Play(lurch);

        if (speedGauge_ !is null) {
            speedGauge_.PlaySlowdownFlash(dropped);
        }
        PlayParams sound;
        sound.bus = AudioBus::SE;
        sound.volume = 0.7f * (0.6f + 0.4f * strength);
        sound.pitch = 0.55f;
        Audio::PlayOneShot("Application/Assets/Sounds/SE/build.mp3", sound);
    }

    // 終わりが決まった瞬間に結果を確定し、トロッコとカーソルを止める
    private void OnEnding(bool isClear)
    {
        GameResult::SetHorizontalProgressBlocks(train_.GetHorizontalProgressBlocks());
        GameResult::SetMonkeyCount(hunger_.GetMonkeyCount());
        if (!isClear) {
            train_.PlayGameOverLaunch();
        }
        train_.Stop();
        builder_.Stop();
    }

    // ---------------------------------------------------------------- 探す

    private GameManager@ FindGameManager(const string &in name)
    {
        GameManager@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に GameManager がありません");
        }
        return found;
    }

    private Hunger@ FindHunger(const string &in name)
    {
        Hunger@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に Hunger がありません");
        }
        return found;
    }

    private MapGenerator@ FindMapGenerator(const string &in name)
    {
        MapGenerator@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に MapGenerator がありません");
        }
        return found;
    }

    private MapView@ FindMapView(const string &in name)
    {
        MapView@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に MapView がありません");
        }
        return found;
    }

    private RailPath@ FindRailPath(const string &in name)
    {
        RailPath@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に RailPath がありません");
        }
        return found;
    }

    private RailView@ FindRailView(const string &in name)
    {
        RailView@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に RailView がありません");
        }
        return found;
    }

    private RailBuilder@ FindRailBuilder(const string &in name)
    {
        RailBuilder@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に RailBuilder がありません");
        }
        return found;
    }

    private TrainMovement@ FindTrainMovement(const string &in name)
    {
        TrainMovement@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に TrainMovement がありません");
        }
        return found;
    }

    private StaminaGauge@ FindStaminaGauge(const string &in name)
    {
        StaminaGauge@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に StaminaGauge がありません");
        }
        return found;
    }

    private SpeedGauge@ FindSpeedGauge(const string &in name)
    {
        SpeedGauge@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に SpeedGauge がありません");
        }
        return found;
    }

    private GameEntrance@ FindGameEntrance(const string &in name)
    {
        GameEntrance@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に GameEntrance がありません");
        }
        return found;
    }

    private PauseMenu@ FindPauseMenu(const string &in name)
    {
        PauseMenu@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に PauseMenu がありません");
        }
        return found;
    }

    private BananaHarvest@ FindBananaHarvest(const string &in name)
    {
        BananaHarvest@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に BananaHarvest がありません");
        }
        return found;
    }

    private RockThrow@ FindRockThrow(const string &in name)
    {
        RockThrow@ found;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@found)) {
            Warn("GameDirector: " + name + " に RockThrow がありません");
        }
        return found;
    }
}
