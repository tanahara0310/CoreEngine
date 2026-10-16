using namespace CoreEngine;

// ResultScene の進行役。背景と UI を組み、左右で「もういちど／タイトルへ」を選んで決定、戻る（ESC）でタイトルへ。
// カメラは背景の中心を見ながらゆっくり回る（サルが多いほど引く）。Tips は入るたびに次の 1 つを出す
[DisplayName("ResultScene の進行役")]
class ResultDirector : ScriptComponent
{
    [DisplayName("Tips（上から順に 1 つずつ出す）")]
    array<string> tips = {
        "エキ　ハイルト　オソクナル。サル　フエル。",
        "レール　ヒク　スタミナ　ヘル。",
        "バナナ　スタミナ　カイフク　デキル。",
        "サル　オオイ　スタミナ　ヘリヤスイ。",
        "サル　オオイ　ハヤクナルノ　ハヤイ。",
        "レール　ナガクナル　オソク　ナリヅライ。",
        "イワ　コワス　スタミナ　ヨク　ヘル。",
        "ミズ　レール　ヒク　スタミナ　ヨク　ヘル。",
        "サル　イマ　デヨウ　ト　モガイテル。"
    };

    [DisplayName("BGM")]
    string bgmPath = "Application/Assets/Sounds/BGM/Result_bgm.mp3";

    [DisplayName("BGM の音量"), Range(0, 1)]
    float bgmVolume = 1.0f / 3.0f;

    [DisplayName("カメラの周回の速さ（rad/秒）"), Range(-2, 2)]
    float cameraOrbitSpeed = 0.32f;

    [DisplayName("サルを並べる円の半径"), Range(0, 30)]
    float monkeyRingRadius = 4.5f;

    [DisplayName("目標距離（m）"), Range(50, 5000)]
    float goalMeters = 500.0f;

    [DisplayName("カメラのオブジェクトの名前")]
    string cameraName = "MainCamera";

    private ResultStage stage_;
    private ResultGauge gauge_;
    private Sound@ bgm_;
    private GameObject@ cameraObject_;
    private bool titleSelected_ = false;
    private bool returnRequested_ = false;
    private string nextScene_;
    private float orbitAngle_ = 0.0f;

    void Start() override
    {
        stage_.monkeyRingRadius = monkeyRingRadius;
        stage_.Build(owner, GameResult::GetMonkeyCount(), GameResult::GetBrokenRockCount(), GameResult::GetBananaHarvestCount(),
            GameResult::GetHorizontalProgressBlocks());

        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        params.volume = bgmVolume;
        @bgm_ = Audio::PlayScoped(bgmPath, params);

        // 1 マス 1m
        gauge_.goalMeters = goalMeters;
        gauge_.Build(owner, GameResult::GetHorizontalProgressBlocks(), GameResult::GetMonkeyCount());
        gauge_.retry.button.SetSelected(true);
        gauge_.title.button.SetSelected(false);
        gauge_.retry.button.Start();
        gauge_.title.button.Start();
        ShowNextTip();

        @cameraObject_ = owner.FindObject(cameraName);
    }

    void Update() override
    {
        const float deltaTime = Time::UnscaledDeltaTime();
        stage_.Tick(deltaTime);
        gauge_.Tick(deltaTime);
        if (returnRequested_) {
            return;
        }
        if (Input::IsActionTriggered(InputAction::Back)) {
            returnRequested_ = true;
            Scene::ChangeScene("TitleScene");
            return;
        }
        const bool left = Input::IsActionTriggered(InputAction::MoveLeft);
        const bool right = Input::IsActionTriggered(InputAction::MoveRight);
        if (left != right) {
            SetSelection(!titleSelected_, true);
            PlaySe("Application/Assets/Sounds/SE/rail_build.mp3");
        }
        if (Input::IsActionTriggered(InputAction::Decide)) {
            Confirm();
        }
    }

    // 配置の端まで収まるよう、サルが多いほどカメラを引いて回す
    void LateUpdate() override
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (camera is null) {
            return;
        }
        const float distance = Max(18.0f, stage_.SceneRadius() + 14.0f);
        orbitAngle_ += cameraOrbitSpeed * Max(0.0f, Time::UnscaledDeltaTime());
        orbitAngle_ = Wrap(orbitAngle_, kTau);
        const Vector3 position = Vector3(sin(orbitAngle_) * distance, ResultLayout::kMonkeyY + 5.5f, -cos(orbitAngle_) * distance);
        camera.position = position;
        camera.LookAt(Vector3(0.0f, ResultLayout::kMonkeyY, 0.0f));
        camera.UpdateMatrix();
        if (cameraObject_ !is null) {
            cameraObject_.transform.position = position;
            cameraObject_.transform.rotation = camera.rotation;
        }
    }

    void OnDestroy() override
    {
        @bgm_ = null;
    }

    // ---------------------------------------------------------------- 選ぶ

    private void SetSelection(bool titleSelected, bool playReaction)
    {
        titleSelected_ = titleSelected;
        gauge_.retry.button.SetSelected(!titleSelected_);
        gauge_.title.button.SetSelected(titleSelected_);
        if (playReaction) {
            SelectedButton().PlaySelectionReaction();
        }
    }

    // 選んだ札が膨らんで消え、選ばなかった札が縮んで消えてからシーンを移る
    private void Confirm()
    {
        returnRequested_ = true;
        PlaySe("Application/Assets/Sounds/SE/decision.mp3");
        nextScene_ = titleSelected_ ? "TitleScene" : "GameScene";
        UnselectedButton().PlayUnselectedFade();
        SelectedButton().PlayConfirmReaction(TweenCallback(this.ChangeToNextScene));
    }

    private ResultButton@ SelectedButton()
    {
        if (titleSelected_) {
            return gauge_.title.button;
        }
        return gauge_.retry.button;
    }

    private ResultButton@ UnselectedButton()
    {
        if (titleSelected_) {
            return gauge_.retry.button;
        }
        return gauge_.title.button;
    }

    private void ChangeToNextScene()
    {
        Scene::ChangeScene(nextScene_);
    }

    // 空欄を飛ばしつつ、前に出した次の Tips を出す（順番はシーンをまたいで覚える）
    private void ShowNextTip()
    {
        if (tips.length() == 0) {
            return;
        }
        const int count = int(tips.length());
        const int next = Session::GetInt("Result.NextTipIndex", 0);
        for (int offset = 0; offset < count; ++offset) {
            const int index = (next + offset) % count;
            if (tips[index].length() == 0) {
                continue;
            }
            gauge_.SetTipText(tips[index]);
            Session::SetInt("Result.NextTipIndex", (index + 1) % count);
            return;
        }
    }

    private void PlaySe(const string &in path)
    {
        PlayParams params;
        params.bus = AudioBus::SE;
        Audio::PlayOneShot(path, params);
    }
}
