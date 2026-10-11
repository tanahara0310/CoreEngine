// 「ウィンドウ」のタブ。メニューから関数を呼ぶ方法と、ウィンドウが呼ばれる順番
//
//   [MenuItem("Tools/テスト/名前")]  クラスに付けると、メニューから開くウィンドウになる（最後の区切りが題名）
//                                    クラスの外の、引数も戻り値も無い関数に付けると、選ぶたびに 1 回呼ぶ
//                                    区切り（/）を増やすと入れ子のメニューになる
//   void OnEnable()    開いたとき（.as を保存して読み直した後に開いていたときも）
//   void OnGUI()       開いている間、毎フレーム
//   void OnDisable()   閉じたとき（読み直す前に開いていたときも）
//   Log・Warn・Error   Console へ出す
// メニューの関数は、ウィンドウを開いていなくても呼べる。下の 2 つは Tools > テスト にある。

// 呼んだ回数（スクリプトの全体の変数は、読み直すと 0 に戻る）
int g_featureTestMenuCalls = 0;

[MenuItem("Tools/テスト/メニューから関数を呼ぶ")]
void FeatureTestMenuFunction()
{
    ++g_featureTestMenuCalls;
    Log("エディタ拡張のテスト：メニューから関数を呼んだ（" + g_featureTestMenuCalls + " 回目）");
}

[MenuItem("Tools/テスト/選んだオブジェクトの名前を出す")]
void FeatureTestLogSelection()
{
    GameObject@ selected = Editor::GetSelection();
    if (selected is null) {
        Warn("エディタ拡張のテスト：オブジェクトを選んでから実行してください");
        return;
    }
    Log("エディタ拡張のテスト：選んでいるのは " + selected.name);
}

class FeatureTestWindowTab
{
    FeatureTestChecklist checks;
    int enableCount = 0;
    int disableCount = 0;
    private int frames_ = 0;

    array<string> CheckItems()
    {
        array<string> items = {
            "Tools > テスト > メニューから関数を呼ぶ で、Console にログが出て「呼んだ回数」が増える",
            "Tools > テスト > 選んだオブジェクトの名前を出す で、選んでいれば名前、選んでいなければ警告が出る",
            "このウィンドウを閉じて開き直すと、OnDisable と OnEnable の回数が 1 つずつ増える",
            "開いている間は OnGUI の回数が増え続け、ほかのウィンドウのタブの後ろに隠すと止まる",
            "どれかの .as を保存すると、OnDisable → OnEnable が呼ばれ、「呼んだ回数」は 0 に戻る",
            "Log・Warn・Error のボタンで、Console にそれぞれの色で出る"
        };
        return items;
    }

    void OnEnable()
    {
        ++enableCount;
    }

    void OnDisable()
    {
        ++disableCount;
    }

    void CountFrame()
    {
        ++frames_;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("メニューの関数");
        EditorGUI::Label("呼んだ回数", "" + g_featureTestMenuCalls);
        if (EditorGUI::Button("ここからも呼ぶ")) {
            FeatureTestMenuFunction();
        }

        EditorGUI::Header("ウィンドウが呼ばれた回数");
        EditorGUI::Label("OnEnable", "" + enableCount);
        EditorGUI::Label("OnDisable", "" + disableCount);
        EditorGUI::Label("OnGUI", "" + frames_);

        EditorGUI::Header("Console へ出す");
        if (EditorGUI::Button("Log")) {
            Log("エディタ拡張のテスト：Log");
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("Warn")) {
            Warn("エディタ拡張のテスト：Warn");
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("Error")) {
            Error("エディタ拡張のテスト：Error");
        }
    }
}
