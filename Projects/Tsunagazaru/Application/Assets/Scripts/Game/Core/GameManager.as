// ゲームの進み具合
enum GamePhase
{
    Playing = 0,
    Ending = 1,
    Transitioning = 2
}

// 終わりが決まった瞬間に呼ぶ関数（isClear はクリアか）
funcdef void GameEndingCallback(bool isClear);

// ゲームの進行。最初に決まった終わり方（ゲームオーバー／クリア）を覚え、
// 終わりの演出の後、スケールされない時間で待ってからリザルトへ移る
[DisplayName("ゲームの進行")]
class GameManager : ScriptComponent
{
    [DisplayName("リザルトへ移るまでの待ち（秒）"), Range(0, 10)]
    float resultTransitionDelay = 1.0f;

    [DisplayName("リザルトのシーン")]
    string resultScene = "ResultScene";

    [DisplayName("クリアで寄るカメラリグ")]
    string closeUpRig = "Train_CloseUp";

    [DisplayName("クリアで寄り切るまでの時間（秒）"), Range(0, 10)]
    float closeUpSeconds = 1.5f;

    private int phase_ = GamePhase::Playing;
    private bool isGameClear_ = false;
    private bool isGameOver_ = false;
    private float changeDelayTimer_ = 0.0f;
    private float closeUpTimer_ = 0.0f;
    private GameEndingCallback@ onEnding_;

    int GetPhase() const { return phase_; }
    bool IsPlaying() const { return phase_ == GamePhase::Playing; }
    bool IsGameOver() const { return isGameOver_; }
    bool IsGameClear() const { return isGameClear_; }

    // 終わりが決まった瞬間に呼ぶ関数を渡す（結果の確定と、トロッコ・カーソルを止めるのは呼ばれた側）
    void SetOnEnding(GameEndingCallback@ callback)
    {
        @onEnding_ = callback;
    }

    void RequestGameOver(float changeDelayTime = -1.0f)
    {
        BeginEnding(false, changeDelayTime);
    }

    void RequestGameClear(float changeDelayTime = -1.0f)
    {
        BeginEnding(true, changeDelayTime);
    }

    // GameDirector が LateUpdate から呼ぶ
    void TickLate()
    {
        if (phase_ == GamePhase::Ending) {
            UpdateEnding();
        }
    }

    // 最初の終わり方だけを残し、重ねて呼ばれても待ちを始め直さない
    private void BeginEnding(bool isClear, float changeDelayTime)
    {
        if (phase_ != GamePhase::Playing) {
            return;
        }

        isGameClear_ = isClear;
        isGameOver_ = !isClear;
        phase_ = GamePhase::Ending;
        changeDelayTimer_ = (changeDelayTime >= 0.0f) ? changeDelayTime : resultTransitionDelay;

        if (onEnding_ !is null) {
            onEnding_(isClear);
        }

        // クリアのときだけトロッコへ寄る。寄り切ってから待ちを始める
        closeUpTimer_ = 0.0f;
        if (isClear) {
            closeUpTimer_ = CameraRig::Activate(closeUpRig, closeUpSeconds, true) ? closeUpSeconds : 0.0f;
        }

        Log("GameManager: " + (isClear ? "ゲームクリア" : "ゲームオーバー") + "。終わりの演出の後 "
            + formatFloat(changeDelayTimer_, "", 0, 2) + " 秒でリザルトへ");
    }

    // ポーズやスローでも止まらない時間で進める
    private void UpdateEnding()
    {
        const float deltaTime = Max(0.0f, Time::UnscaledDeltaTime());
        if (closeUpTimer_ > 0.0f) {
            closeUpTimer_ -= deltaTime;
            return;
        }
        changeDelayTimer_ -= deltaTime;
        if (changeDelayTimer_ <= 0.0f) {
            phase_ = GamePhase::Transitioning;
            Scene::ChangeScene(resultScene);
        }
    }
}
