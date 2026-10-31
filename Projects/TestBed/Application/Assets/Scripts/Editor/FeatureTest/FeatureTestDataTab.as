using namespace CoreEngine;

// 「データ」のタブ。ゲームのデータ（Application/Assets の下の JSON）を、エディタの道具で書いてゲームで読む使い方
//
//   DataFile@ file = DataFile("Data/名前.json");   Application/Assets からの相対パス（.json）。無ければ空の中身で開く
//   file.exists                                     ファイルがあるか
//   file.Has(キー)・GetKeys()                        キーがあるか・キーの一覧
//   file.GetInt・GetFloat・GetBool・GetString(キー, 無いときの値)
//   file.GetIntArray・GetFloatArray・GetStringArray(キー)
//   file.SetInt・SetFloat・SetBool・SetString・SetIntArray・SetFloatArray・SetStringArray(キー, 値)
//   file.Remove(キー)
//   file.Save()                                     書き出す（エディタでだけ書ける。書き出したゲームでは読むだけ）
// 遊んだ記録（Application/Saved の下）は SaveFile で、使い方は同じ。
class FeatureTestDataTab
{
    FeatureTestChecklist checks;
    string path = "Data/FeatureTest/Sample.json";
    string title = "テストのデータ";
    int level = 3;
    float speed = 1.5f;
    bool hard = false;
    array<int> scores = { 100, 250, 400 };
    string newKey = "メモ";
    string newValue = "あとで消す";
    string lastResult = "（まだ）";
    private DataFile@ file_;
    private string openedPath_ = "";

    array<string> CheckItems()
    {
        array<string> items = {
            "「保存する」で Application/Assets/Data/FeatureTest/Sample.json ができ、Project に出る",
            "値を変えて保存し、「読み直す」で同じ値が戻る",
            "キーの一覧に、保存したキーと型ごとの値が出る",
            "「キーを足す」で一覧に増え、保存しないで読み直すと消える",
            "一覧の「消す」でそのキーが消える（保存すると、ファイルからも消える）",
            "パスを無いファイルにすると「ファイル」が ない になる",
            "パスに .. や .json で終わらない名前を入れると「使えないパス」と出る（DataFile に渡す前に調べている）"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        path = EditorGUI::TextField("パス", path);
        if (!IsUsablePath(path)) {
            EditorGUI::HelpBox("使えないパスです（Application/Assets からの相対パスで .json で終わり、.. を含まず、Scenes フォルダの外）", EditorGUI::MessageType::Error);
            return;
        }
        if (file_ is null || openedPath_ != path) {
            Open();
        }
        EditorGUI::Label("ファイル", file_.exists ? "ある" : "ない（保存するとできる）");
        EditorGUI::Label("最後の操作", lastResult);

        EditorGUI::Header("書く値");
        title = EditorGUI::TextField("題名", title);
        level = EditorGUI::IntSlider("段階", level, 1, 10);
        speed = EditorGUI::FloatField("速さ", speed);
        hard = EditorGUI::Toggle("難しい", hard);
        EditorGUI::Label("点数の並び", Join(scores));
        if (EditorGUI::Button("点数を足す")) {
            scores.insertLast(scores.length() == 0 ? 100 : scores[scores.length() - 1] + 150);
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("点数を空にする")) {
            scores.resize(0);
        }

        if (EditorGUI::Button("保存する")) {
            file_.SetString("title", title);
            file_.SetInt("level", level);
            file_.SetFloat("speed", speed);
            file_.SetBool("hard", hard);
            file_.SetIntArray("scores", scores);
            lastResult = file_.Save() ? "保存した" : "保存できなかった";
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("読み直す")) {
            Open();
            title = file_.GetString("title", title);
            level = file_.GetInt("level", level);
            speed = file_.GetFloat("speed", speed);
            hard = file_.GetBool("hard", hard);
            if (file_.Has("scores")) {
                scores = file_.GetIntArray("scores");
            }
            lastResult = file_.exists ? "読み直した" : "ファイルが無いので読めなかった";
        }

        EditorGUI::Header("キーの一覧（読んだ中身）");
        newKey = EditorGUI::TextField("足すキー", newKey);
        newValue = EditorGUI::TextField("足す値", newValue);
        if (EditorGUI::Button("キーを足す") && newKey != "") {
            file_.SetString(newKey, newValue);
            lastResult = "キー " + newKey + " を足した（まだ保存していない）";
        }
        DrawKeys();
    }

    private void Open()
    {
        @file_ = DataFile(path);
        openedPath_ = path;
    }

    private void DrawKeys()
    {
        array<string>@ keys = file_.GetKeys();
        if (keys.length() == 0) {
            EditorGUI::TextDisabled("（キーはありません）");
            return;
        }
        if (EditorGUI::BeginTable("keys", 3)) {
            EditorGUI::TableSetupColumn("キー", 110);
            EditorGUI::TableSetupColumn("値");
            EditorGUI::TableSetupColumn("", 60);
            EditorGUI::TableHeadersRow();
            for (uint i = 0; i < keys.length(); ++i) {
                EditorGUI::PushID(int(i));
                EditorGUI::TableNextRow();
                EditorGUI::TableNextColumn();
                EditorGUI::Label(keys[i]);
                EditorGUI::TableNextColumn();
                EditorGUI::Label(ValueText(keys[i]));
                EditorGUI::TableNextColumn();
                if (EditorGUI::Button("消す")) {
                    file_.Remove(keys[i]);
                    lastResult = "キー " + keys[i] + " を消した（まだ保存していない）";
                }
                EditorGUI::PopID();
            }
        }
        EditorGUI::EndTable();
    }

    // 書いた型が分かるキーは型に合わせて読み、ほかは文字として読む
    private string ValueText(const string &in key)
    {
        if (key == "level") {
            return "整数 " + file_.GetInt(key);
        }
        if (key == "speed") {
            return "小数 " + formatFloat(file_.GetFloat(key), "", 0, 2);
        }
        if (key == "hard") {
            return "真偽 " + (file_.GetBool(key) ? "true" : "false");
        }
        if (key == "scores") {
            return "整数の並び " + Join(file_.GetIntArray(key));
        }
        return "文字 " + file_.GetString(key);
    }

    private string Join(const array<int>@ values)
    {
        if (values is null || values.length() == 0) {
            return "（なし）";
        }
        string result = "";
        for (uint i = 0; i < values.length(); ++i) {
            result += (i == 0 ? "" : ", ") + values[i];
        }
        return result;
    }

    private bool IsUsablePath(const string &in value)
    {
        if (value.length() < 6 || value.findFirst("..") >= 0 || value.findFirst(":") >= 0) {
            return false;
        }
        if (value[0] == 47 || value[0] == 92 || value.findFirst("Scenes/") == 0) {
            return false;
        }
        return value.substr(value.length() - 5) == ".json";
    }
}
