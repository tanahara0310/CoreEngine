// 「読み直し」のタブ。.as を保存して読み直したときと、エディタを開き直したときに、ウィンドウの値がどう残るか
//
// 読み直し（.as を保存したとき）
//   ウィンドウのメンバ変数は、公開も private も値を持ち越す。扱える型は 数・bool・string・Vector2〜4・列挙・
//   スクリプトのクラス（ハンドルも）と、それらの array。同じオブジェクトを指すハンドルは同じものを指したまま、
//   派生クラスのオブジェクトは派生クラスのまま作り直す（クラスは引数の無いコンストラクタで作る）。
//   GameObject@ などエンジンの型のハンドルは持ち越さない（null に戻る）。スクリプト全体の変数は初期値に戻る。
// 保存（エディタを閉じたとき）
//   ウィンドウ直下の公開メンバ変数を Saved/EditorSettings/ScriptWindows.json へ書き、次の起動でウィンドウを作るときに戻す。
//   ウィンドウ直下の private は保存しない。公開メンバ変数の中のクラスは、中身（private も）ごと保存する。
class FeatureTestReloadTab
{
    FeatureTestChecklist checks;
    int number = 0;
    float real = 0.0f;
    bool flag = false;
    string text = "";
    Vector2 vector2 = Vector2(0.0f, 0.0f);
    Vector3 vector3 = Vector3(0.0f, 0.0f, 0.0f);
    Vector4 vector4 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    FeatureTestColor favorite = FeatureTestColor::Red;
    array<int> numbers;
    array<string> words;
    FeatureTestItem@ item;
    array<FeatureTestItem@> itemList;
    FeatureTestItem@ shared;
    FeatureTestItem@ derived;
    private int hidden_ = 0;

    array<string> CheckItems()
    {
        array<string> items = {
            "「値を変える」を押すと、下の値が全部変わる",
            "どれかの .as を保存して読み直すと、変えた値がそのまま残る（数・真偽・文字・Vector・列挙・配列・クラス）",
            "読み直した後も「同じものを指している」が はい",
            "読み直した後も「派生クラスのまま」が はい",
            "「ウィンドウの数を増やす」の後で読み直すと、公開も private も残る",
            "エディタを閉じて開き直すと、ウィンドウの公開の数は残り、private の数は 0 に戻る",
            "エディタを閉じて開き直しても、このタブの値（private も）が残る",
            "「最初に戻す」で全部が初めの値になる"
        };
        return items;
    }

    // ウィンドウ直下の数を受け取って出す。「ウィンドウの数を増やす」を押したフレームは true
    bool Draw(int windowPublic, int windowPrivate)
    {
        checks.Draw(CheckItems());
        EditorGUI::HelpBox("「値を変える」を押してから、どれかの .as を保存してください（中身を変えずに保存してもよい）。読み直した後も同じ値なら持ち越せています。");
        if (EditorGUI::Button("値を変える")) {
            Change();
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("最初に戻す")) {
            Reset();
        }

        EditorGUI::Header("このタブのメンバ変数");
        EditorGUI::Label("int", "" + number);
        EditorGUI::Label("float", formatFloat(real, "", 0, 2));
        EditorGUI::Label("bool", flag ? "true" : "false");
        EditorGUI::Label("string", text == "" ? "（空）" : text);
        EditorGUI::Label("Vector2", formatFloat(vector2.x, "", 0, 1) + ", " + formatFloat(vector2.y, "", 0, 1));
        EditorGUI::Label("Vector3", formatFloat(vector3.x, "", 0, 1) + ", " + formatFloat(vector3.y, "", 0, 1) + ", " + formatFloat(vector3.z, "", 0, 1));
        EditorGUI::Label("Vector4", formatFloat(vector4.x, "", 0, 1) + ", " + formatFloat(vector4.w, "", 0, 1) + "（x と w）");
        EditorGUI::Label("列挙", ColorName(favorite));
        EditorGUI::Label("array<int>", Join(numbers));
        EditorGUI::Label("array<string>", JoinWords(words));
        EditorGUI::Label("クラスのハンドル", item is null ? "null" : item.name + " ×" + item.count);
        EditorGUI::Label("クラスの配列", "" + itemList.length() + " 個");
        EditorGUI::Label("同じものを指している", shared !is null && itemList.length() > 0 && shared is itemList[0] ? "はい" : "いいえ（まだ値を変えていない）");
        EditorGUI::Label("派生クラスのまま", cast<FeatureTestRareItem>(derived) !is null ? "はい（珍しさ " + cast<FeatureTestRareItem>(derived).rarity + "）" : "いいえ（まだ値を変えていない）");
        EditorGUI::Label("private の数", "" + hidden_);

        EditorGUI::Header("ウィンドウ直下のメンバ変数");
        EditorGUI::TextDisabled("EditorFeatureTest の windowPublicCount と windowPrivateCount_");
        EditorGUI::Label("公開の数", "" + windowPublic);
        EditorGUI::Label("private の数", "" + windowPrivate);
        return EditorGUI::Button("ウィンドウの数を増やす");
    }

    private void Change()
    {
        ++number;
        real += 0.5f;
        flag = !flag;
        text += "あ";
        vector2 += Vector2(1.0f, 2.0f);
        vector3 += Vector3(1.0f, 2.0f, 3.0f);
        vector4 += Vector4(1.0f, 0.0f, 0.0f, 0.5f);
        favorite = favorite == FeatureTestColor::Red ? FeatureTestColor::Green
            : favorite == FeatureTestColor::Green ? FeatureTestColor::Blue : FeatureTestColor::Red;
        numbers.insertLast(number * 10);
        words.insertLast("言葉" + number);

        if (item is null) {
            @item = FeatureTestItem();
            item.name = "薬草";
        }
        ++item.count;
        FeatureTestItem added;
        added.name = "道具" + (itemList.length() + 1);
        itemList.insertLast(added);
        @shared = itemList[0];
        FeatureTestRareItem rare;
        rare.name = "珍しい石";
        rare.rarity = number;
        @derived = rare;
        ++hidden_;
    }

    private void Reset()
    {
        number = 0;
        real = 0.0f;
        flag = false;
        text = "";
        vector2 = Vector2(0.0f, 0.0f);
        vector3 = Vector3(0.0f, 0.0f, 0.0f);
        vector4 = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        favorite = FeatureTestColor::Red;
        numbers.resize(0);
        words.resize(0);
        @item = null;
        itemList.resize(0);
        @shared = null;
        @derived = null;
        hidden_ = 0;
    }

    private string ColorName(FeatureTestColor value)
    {
        return value == FeatureTestColor::Red ? "Red" : value == FeatureTestColor::Green ? "Green" : "Blue";
    }

    private string JoinWords(const array<string> &in values)
    {
        if (values.length() == 0) {
            return "（空）";
        }
        string result = "";
        for (uint i = 0; i < values.length(); ++i) {
            result += (i == 0 ? "" : "・") + values[i];
        }
        return result;
    }

    private string Join(const array<int> &in values)
    {
        if (values.length() == 0) {
            return "（空）";
        }
        string result = "";
        for (uint i = 0; i < values.length(); ++i) {
            result += (i == 0 ? "" : ", ") + values[i];
        }
        return result;
    }
}

// 読み直しのタブで使う列挙とクラス
enum FeatureTestColor
{
    Red,
    Green,
    Blue
}

class FeatureTestItem
{
    string name = "";
    int count = 0;
}

class FeatureTestRareItem : FeatureTestItem
{
    int rarity = 0;
}
