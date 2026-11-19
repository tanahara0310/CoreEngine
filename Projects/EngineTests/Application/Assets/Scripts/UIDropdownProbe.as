using namespace CoreEngine;

// ドロップダウンの試験：最初にスクリプトからの操作を確かめ、その後は値の変化・開け閉め・一覧の位置と高さ・
// 開いている間に押せてはいけないボタンを Console に出す。右の Status に各ドロップダウンの値を出す
[DisplayName("試験：UI ドロップダウン")]
class UIDropdownProbe : ScriptComponent
{
    private array<string> names_ = { "Quality", "Long", "Scripted", "Disabled", "Masked", "Bottom" };
    private array<UIDropdown@> dropdowns_;
    private array<bool> wasOpen_;
    private array<int> checkFrame_;
    private UIText@ status_;
    private int frame_ = 0;
    private int lastOpenFrame_ = -1000;

    void Start()
    {
        @status_ = owner.FindObject("Status").uiText;
        for (uint i = 0; i < names_.length(); ++i) {
            dropdowns_.insertLast(owner.FindObject(names_[i]).uiDropdown);
            wasOpen_.insertLast(false);
            checkFrame_.insertLast(-1);
        }
    }

    void Update()
    {
        ++frame_;
        if (frame_ == 5) {
            CheckScript();
        }
        if (frame_ == 10) {
            // キー・パッドの上下左右で選び始められるよう、外のボタンを選んでおく
            owner.FindObject("OutsideButton").uiButton.Focus();
        }
        ReportChanges();
        ReportBlocked();
        UpdateStatus();
    }

    // スクリプトから選択肢を入れ替え、値を変え、開け閉めする
    void CheckScript()
    {
        UIDropdown@ d = dropdowns_[2];
        d.ClearOptions();
        Log("UIDropdownProbe: ClearOptions の後 選択肢 " + d.options.length() + " 個・値 " + d.value + "（期待 0 個・0）");
        d.AddOption("赤");
        d.AddOption("緑");
        d.AddOption("青");
        Log("UIDropdownProbe: AddOption を 3 回の後 選択肢 " + Join(d.options) + "・今の値の文字 " + Caption("Scripted")
            + "（期待 赤・緑・青・赤）");
        array<string> seasons = { "春", "夏", "秋", "冬" };
        d.options = seasons;
        d.value = 2;
        Log("UIDropdownProbe: options を 4 つにして値を 2 にすると 今の値の文字 " + Caption("Scripted") + "（期待 秋）");
        d.RemoveOption(0);
        Log("UIDropdownProbe: RemoveOption(0) の後 選択肢 " + Join(d.options) + "・値 " + d.value + "・今の値の文字 "
            + Caption("Scripted") + "（期待 夏・秋・冬・1・秋）");
        d.value = 99;
        Log("UIDropdownProbe: 値を 99 にすると " + d.value + "（期待 2）");
        d.value = 1;
        d.Open();
        Log("UIDropdownProbe: Open() の後 isOpen " + d.isOpen + "（期待 true）");
        d.Close();
        Log("UIDropdownProbe: Close() の後 isOpen " + d.isOpen + "（期待 false）");
        dropdowns_[3].Open();
        Log("UIDropdownProbe: 押せない Disabled に Open() すると isOpen " + dropdowns_[3].isOpen + "（期待 false）");
    }

    // 値が変わった・開いた・閉じたを出し、開いた次のフレームに一覧の位置と高さを確かめる
    void ReportChanges()
    {
        for (uint i = 0; i < dropdowns_.length(); ++i) {
            UIDropdown@ d = dropdowns_[i];
            if (d.wasChanged) {
                Log("UIDropdownProbe: " + names_[i] + " の値が " + d.value + "（" + OptionText(d) + "）になった");
            }
            bool open = d.isOpen;
            if (open) {
                lastOpenFrame_ = frame_;
            }
            if (open != wasOpen_[i]) {
                wasOpen_[i] = open;
                Log("UIDropdownProbe: " + names_[i] + (open ? " を開いた" : " を閉じた"));
                checkFrame_[i] = open ? frame_ + 1 : -1;
            }
            if (open && frame_ == checkFrame_[i]) {
                CheckList(i);
            }
        }
    }

    void CheckList(uint i)
    {
        string name = names_[i];
        Vector4 button = owner.FindObject(name).rectTransform.rect;
        Vector4 list = owner.FindObject(name + " Template").rectTransform.rect;
        bool below = list.y >= button.y + button.w - 1.0f;
        bool inside = list.y >= -0.5f && list.y + list.w <= 1080.5f;
        string expected = name == "Bottom" ? "上" : "下";
        Log("UIDropdownProbe: " + name + " の一覧は押す所の" + (below ? "下" : "上") + "・高さ " + int(list.w + 0.5f)
            + "・画面に" + (inside ? "収まる" : "収まらない") + "（期待 " + expected + "・" + ExpectedHeight(name)
            + "・収まる）");

        GameObject@ row = owner.FindObject(name + " Item " + dropdowns_[i].value);
        if (row is null) {
            Log("UIDropdownProbe: " + name + " の今の値の行が見つからない");
            return;
        }
        Vector4 r = row.rectTransform.rect;
        bool visible = r.y >= list.y - 0.5f && r.y + r.w <= list.y + list.w + 0.5f;
        Log("UIDropdownProbe: " + name + " の今の値の行は一覧の" + (visible ? "中に見えている" : "外") + "（期待 中に見えている）");
    }

    // 選択肢が一覧に入るなら行の数だけに縮み、入らなければ決めた高さのまま
    string ExpectedHeight(const string&in name)
    {
        if (name == "Long") {
            return "320";
        }
        uint count = dropdowns_[names_.find(name)].options.length();
        return "" + (count * 36 + 8);
    }

    // 開いている間（と閉じた直後）に、一覧の外のボタンが押されたら知らせる
    void ReportBlocked()
    {
        string[] buttons = { "BlockButton", "OutsideButton" };
        for (uint i = 0; i < buttons.length(); ++i) {
            if (owner.FindObject(buttons[i]).uiButton.wasClicked) {
                bool recent = frame_ - lastOpenFrame_ <= 2;
                Log("UIDropdownProbe: " + buttons[i] + " が押された" + (recent ? "（一覧が開いていたので押せてはいけない）" : ""));
            }
        }
    }

    void UpdateStatus()
    {
        string text = "";
        for (uint i = 0; i < dropdowns_.length(); ++i) {
            UIDropdown@ d = dropdowns_[i];
            text += names_[i] + "：" + d.value + "（" + OptionText(d) + "）" + (d.isOpen ? " 開いている" : "") + "\n";
        }
        status_.text = text;
    }

    string OptionText(UIDropdown@ d)
    {
        array<string>@ options = d.options;
        return d.value >= 0 && uint(d.value) < options.length() ? options[d.value] : "なし";
    }

    string Caption(const string&in name)
    {
        return owner.FindObject(name + " Label").uiText.text;
    }

    string Join(const array<string>&in items)
    {
        string text = "";
        for (uint i = 0; i < items.length(); ++i) {
            text += (i > 0 ? "・" : "") + items[i];
        }
        return text;
    }
}
