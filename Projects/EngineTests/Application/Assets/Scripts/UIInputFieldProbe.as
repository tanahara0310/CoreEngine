using namespace CoreEngine;

// 入力欄の試験：最初にスクリプトからの操作を確かめ、その後は文字の変化・確定・打ち始めと打ち終わりを Console に出す。
// 右の Status に各欄の文字を出す。WASD で動く四角が、打っている間に動いたら知らせる
[DisplayName("試験：UI 入力欄")]
class UIInputFieldProbe : ScriptComponent
{
    private array<string> names_ = { "Name", "Limit", "Integer", "Decimal", "Alnum", "Password", "ReadOnly", "Disabled", "Multi", "Long" };
    private array<UIInputField@> fields_;
    private array<bool> wasFocused_;
    private UIText@ status_;
    private RectTransform@ mover_;
    private int frame_ = 0;
    private bool reportedMove_ = false;
    private bool reportedMoveWhileTyping_ = false;

    void Start()
    {
        @status_ = owner.FindObject("Status").uiText;
        @mover_ = owner.FindObject("Mover").rectTransform;
        for (uint i = 0; i < names_.length(); ++i) {
            fields_.insertLast(owner.FindObject(names_[i]).uiInputField);
            wasFocused_.insertLast(false);
        }
    }

    void Update()
    {
        ++frame_;
        if (frame_ == 5) {
            CheckScript();
        }
        if (frame_ == 6) {
            Field("Name").Focus();
        }
        if (frame_ == 7) {
            Log("UIInputFieldProbe: Focus() の後 isFocused " + Field("Name").isFocused + "（期待 true）");
            Field("Name").Unfocus();
        }
        if (frame_ == 8) {
            Log("UIInputFieldProbe: Unfocus() の後 isFocused " + Field("Name").isFocused + "・wasSubmitted "
                + Field("Name").wasSubmitted + "（期待 false・false）");
            Field("Disabled").Focus();
        }
        if (frame_ == 9) {
            Log("UIInputFieldProbe: 押せない Disabled に Focus() すると isFocused " + Field("Disabled").isFocused + "（期待 false）");
        }
        ReportChanges();
        MoveMover();
        UpdateStatus();
    }

    // 中身の種類と最大の文字数が、スクリプトから入れた文字にも効くか
    void CheckScript()
    {
        Field("Integer").text = "12a-3.4";
        Log("UIInputFieldProbe: 整数だけの欄に 12a-3.4 を入れると " + Field("Integer").text + "（期待 1234）");
        Field("Integer").text = "-42";
        Log("UIInputFieldProbe: 整数だけの欄に -42 を入れると " + Field("Integer").text + "（期待 -42）");
        Field("Decimal").text = "-3.14.15";
        Log("UIInputFieldProbe: 小数の欄に -3.14.15 を入れると " + Field("Decimal").text + "（期待 -3.1415）");
        Field("Alnum").text = "abc あいう 123!";
        Log("UIInputFieldProbe: 英数字だけの欄に「abc あいう 123!」を入れると " + Field("Alnum").text + "（期待 abc123）");
        Field("Limit").text = "あいうえおかきくけこ";
        Log("UIInputFieldProbe: 8 文字までの欄に 10 文字を入れると " + Field("Limit").text + "（期待 あいうえおかきく）");
        Field("Limit").characterLimit = 4;
        Log("UIInputFieldProbe: 最大の文字数を 4 にすると " + Field("Limit").text + "（期待 あいうえ）");
        Field("Limit").characterLimit = 8;
        Field("Password").text = "secret";
        Log("UIInputFieldProbe: 伏せ字の欄に secret を入れると text " + Field("Password").text + "・出す文字 "
            + owner.FindObject("Password Text").uiText.text + "（期待 secret・******）");
        Field("Name").text = "改行\nなし";
        Log("UIInputFieldProbe: 1 行の欄に改行入りの文字を入れると " + Field("Name").text + "（期待 改行なし）");
        Field("Multi").text = "1 行目\r\n2 行目";
        Log("UIInputFieldProbe: 複数行の欄に CRLF 入りの文字を入れると 改行 " + CountLines(Field("Multi").text) + " 行（期待 2）");
        Field("Name").contentType = 1;
        Log("UIInputFieldProbe: 名前の欄を整数だけにすると " + Field("Name").text + "（期待 空）");
        Field("Name").contentType = 0;

        // 手で試す欄は空に戻す
        Field("Integer").text = "";
        Field("Decimal").text = "";
        Field("Alnum").text = "";
        Field("Limit").text = "";
        Field("Password").text = "";
        Field("Multi").text = "";
    }

    // 文字が変わった・確定した・打ち始めた・打ち終えたを出す
    void ReportChanges()
    {
        for (uint i = 0; i < fields_.length(); ++i) {
            UIInputField@ f = fields_[i];
            if (f.wasChanged) {
                Log("UIInputFieldProbe: " + names_[i] + " の文字が「" + f.text + "」になった");
            }
            if (f.wasSubmitted) {
                Log("UIInputFieldProbe: " + names_[i] + " を確定した「" + f.text + "」");
            }
            bool focused = f.isFocused;
            if (focused != wasFocused_[i]) {
                wasFocused_[i] = focused;
                Log("UIInputFieldProbe: " + names_[i] + (focused ? " を打ち始めた" : " を打ち終えた"));
            }
        }
    }

    // WASD（移動のアクション）で四角を動かす。打っている間に動いたら知らせる
    void MoveMover()
    {
        Vector2 axis = Input::GetAxis2D(InputAction::MoveLeft, InputAction::MoveRight,
                                        InputAction::MoveForward, InputAction::MoveBack);
        if (axis.x == 0.0f && axis.y == 0.0f) {
            return;
        }
        float step = 300.0f * Time::UnscaledDeltaTime();
        Vector2 p = mover_.anchoredPosition;
        p.x = Clamp(p.x + axis.x * step, 0.0f, 560.0f);
        p.y = Clamp(p.y + axis.y * step, 0.0f, 260.0f);
        mover_.anchoredPosition = p;
        if (AnyFocused()) {
            if (!reportedMoveWhileTyping_) {
                reportedMoveWhileTyping_ = true;
                Log("UIInputFieldProbe: 打っている間に Mover が動いた（動いてはいけない）");
            }
        } else if (!reportedMove_) {
            reportedMove_ = true;
            Log("UIInputFieldProbe: 打っていない間は Mover が動く");
        }
    }

    void UpdateStatus()
    {
        string text = "";
        for (uint i = 0; i < fields_.length(); ++i) {
            UIInputField@ f = fields_[i];
            string value = f.text;
            if (value.length() > 60) {
                value = "（" + value.length() + " バイトの文）";
            }
            text += names_[i] + "：" + value + (f.isFocused ? "  ← 打っている" : "") + "\n";
        }
        status_.text = text;
    }

    bool AnyFocused()
    {
        for (uint i = 0; i < fields_.length(); ++i) {
            if (fields_[i].isFocused) {
                return true;
            }
        }
        return false;
    }

    UIInputField@ Field(const string&in name)
    {
        return fields_[names_.find(name)];
    }

    int CountLines(const string&in value)
    {
        int lines = 1;
        for (uint i = 0; i < value.length(); ++i) {
            if (value[i] == 10) {
                ++lines;
            }
        }
        return lines;
    }

    float Clamp(float value, float low, float high)
    {
        return value < low ? low : (value > high ? high : value);
    }
}
