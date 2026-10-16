using namespace CoreEngine;

// 「入力」のタブ。ボタンと値の欄、値が変わったかの調べ方
//
//   EditorGUI::Button(文, 幅)            押したフレームだけ true（幅 0 は文に合わせ、-1 は幅いっぱい）
//   EditorGUI::Toggle(ラベル, 値)        チェック。変えた後の値を返すので、同じ変数へ入れ直す
//   EditorGUI::RadioButton(文, 選んでいるか)    押したフレームだけ true
//   EditorGUI::RadioButton(文, 今の値, この値)  押されたら「この値」を、そうでなければ「今の値」を返す
//   EditorGUI::IntField・FloatField・TextField・TextArea(ラベル, 値, 高さ)
//   EditorGUI::IntSlider・Slider(ラベル, 値, 最小, 最大)
//   EditorGUI::Vector2Field・Vector3Field・ColorField(ラベル, 値)
//   EditorGUI::changed                   部品で値が変わると true になる（調べる前に false を入れておく）
//   EditorGUI::IsItemActive()            直前の部品を操作している間（文字の欄なら打っている間）
//   EditorGUI::IsItemEdited()            直前の部品の値をこのフレームで変えた
//   EditorGUI::IsItemDeactivatedAfterEdit()  直前の部品の値を変えて、操作を終えた（Enter・外を押した）
class FeatureTestInputTab
{
    FeatureTestChecklist checks;
    int pressCount = 0;

    bool toggle = true;
    int integer = 3;
    float real = 1.5f;
    string text = "こんにちは";
    string notes = "複数行の\nメモ";
    int level = 5;
    float ratio = 0.5f;
    Vector2 size = Vector2(1.0f, 2.0f);
    Vector3 position = Vector3(0.0f, 1.0f, 0.0f);
    Vector4 color = Vector4(0.9f, 0.6f, 0.2f, 1.0f);
    int changedFrames = 0;

    int mode = 0;
    bool running = false;

    string name = "勇者";
    string committedName = "勇者";
    int editCount = 0;
    private bool nameActive_ = false;

    array<string> CheckItems()
    {
        array<string> items = {
            "ボタンを押すと回数が増え、「0 に戻す」（幅 90）で 0 になる。幅いっぱいのボタンもある",
            "値の欄を変えると「値が変わったフレーム」が増え、触っていない間は増えない",
            "整数と小数の欄は、ドラッグでも打ち込みでも変えられる",
            "スライダーは範囲（段階 1〜10・割合 0〜1）の外へ動かせない",
            "色の欄の四角を押すと、色を選ぶ画面が出る",
            "複数行の欄で Enter を押すと改行になる",
            "ラジオボタンは押した 1 つだけが選ばれる（上の段と下の段の 2 つの書き方の両方）",
            "名前の欄に打っている間は「打っている」が はい、Enter か外を押すと「確定した名前」が変わる"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("ボタン");
        if (EditorGUI::Button("押した回数を増やす")) {
            ++pressCount;
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("0 に戻す", 90)) {
            pressCount = 0;
        }
        if (EditorGUI::Button("幅いっぱいのボタン", -1)) {
            ++pressCount;
        }
        EditorGUI::Label("押した回数", "" + pressCount);

        EditorGUI::Header("値の欄");
        EditorGUI::changed = false;
        toggle = EditorGUI::Toggle("チェック", toggle);
        integer = EditorGUI::IntField("整数", integer);
        real = EditorGUI::FloatField("小数", real);
        text = EditorGUI::TextField("文字", text);
        notes = EditorGUI::TextArea("複数行", notes, 60);
        level = EditorGUI::IntSlider("段階", level, 1, 10);
        ratio = EditorGUI::Slider("割合", ratio, 0.0f, 1.0f);
        size = EditorGUI::Vector2Field("大きさ", size);
        position = EditorGUI::Vector3Field("位置", position);
        color = EditorGUI::ColorField("色", color);
        if (EditorGUI::changed) {
            ++changedFrames;
        }
        EditorGUI::Label("値が変わったフレーム", "" + changedFrames);

        EditorGUI::Header("ラジオボタン");
        // 今の値とこの値を渡す書き方（押されたら、この値が返る）
        mode = EditorGUI::RadioButton("歩く", mode, 0);
        EditorGUI::SameLine();
        mode = EditorGUI::RadioButton("走る", mode, 1);
        EditorGUI::SameLine();
        mode = EditorGUI::RadioButton("飛ぶ", mode, 2);
        // 選んでいるかを渡す書き方（押されたフレームだけ true）
        if (EditorGUI::RadioButton("止まる", !running)) {
            running = false;
        }
        EditorGUI::SameLine();
        if (EditorGUI::RadioButton("動く", running)) {
            running = true;
        }
        EditorGUI::Label("選んだもの", (mode == 0 ? "歩く" : mode == 1 ? "走る" : "飛ぶ") + "・" + (running ? "動く" : "止まる"));

        EditorGUI::Header("部品の状態");
        name = EditorGUI::TextField("名前", name);
        nameActive_ = EditorGUI::IsItemActive();
        if (EditorGUI::IsItemEdited()) {
            ++editCount;
        }
        if (EditorGUI::IsItemDeactivatedAfterEdit()) {
            committedName = name;
        }
        EditorGUI::Label("打っている", nameActive_ ? "はい" : "いいえ");
        EditorGUI::Label("文字を変えた回数", "" + editCount);
        EditorGUI::Label("確定した名前", committedName);
    }
}
