// ボタンが押されたかと、いまキー・パッドで選んでいるものを毎フレーム見て文字へ出す
[DisplayName("UI：ボタンの試験")]
class UIButtonTester : ScriptComponent
{
    [ObjectRef] [Tooltip("中央のボタン")]
    GameObject@ center;

    [ObjectRef] [Tooltip("上のボタン")]
    GameObject@ up;

    [ObjectRef] [Tooltip("下のボタン")]
    GameObject@ down;

    [ObjectRef] [Tooltip("左のボタン")]
    GameObject@ left;

    [ObjectRef] [Tooltip("右のボタン")]
    GameObject@ right;

    [ObjectRef] [Tooltip("回数と選んでいるものを出す文字")]
    GameObject@ label;

    private int count_ = 0;
    private string focused_ = "";

    void Start()
    {
        Refresh();
    }

    void Update()
    {
        CountClick(center);
        CountClick(up);
        CountClick(down);
        CountClick(left);
        CountClick(right);

        string picked = FocusedName();
        if (picked != focused_) {
            focused_ = picked;
            Log("選んでいる: " + (focused_.length() > 0 ? focused_ : "なし"));
            Refresh();
        }
    }

    // 押された回数を数える
    private void CountClick(GameObject@ button)
    {
        if (button is null) {
            return;
        }
        if (button.uiButton.wasClicked) {
            count_ += 1;
            Log("押された: " + button.name + "（" + count_ + " 回目）");
            Refresh();
        }
    }

    // いま選ばれているボタンの名前（無ければ空）
    private string FocusedName()
    {
        array<GameObject@> buttons = { center, up, down, left, right };
        for (uint i = 0; i < buttons.length(); i++) {
            if (buttons[i] !is null && buttons[i].uiButton.focused) {
                return buttons[i].name;
            }
        }
        return "";
    }

    private void Refresh()
    {
        if (label !is null) {
            label.uiText.text = "押された回数: " + count_
                + "   選んでいる: " + (focused_.length() > 0 ? focused_ : "なし");
        }
    }
}
