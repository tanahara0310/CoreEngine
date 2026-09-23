// ボタンが押されたかを毎フレーム見て、押された回数を文字へ出す
[DisplayName("UI：ボタンの試験")]
class UIButtonTester : ScriptComponent
{
    [ObjectRef] [Tooltip("数える対象のボタン")]
    GameObject@ button;

    [ObjectRef] [Tooltip("回数を出す文字")]
    GameObject@ label;

    private int count_ = 0;

    void Start()
    {
        Refresh();
    }

    void Update()
    {
        if (button is null) {
            return;
        }
        if (button.uiButton.wasClicked) {
            count_ += 1;
            Log("ボタンが押された: " + count_ + " 回目");
            Refresh();
        }
    }

    private void Refresh()
    {
        if (label !is null) {
            label.uiText.text = "押された回数: " + count_;
        }
    }
}
