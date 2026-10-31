using namespace CoreEngine;

// 「メニュー」のタブ。メニュー・右クリックのメニュー・確認ダイアログの使い方
//
//   EditorGUI::OpenPopup(ID)                       次から BeginPopup(ID) が開く（ボタンを押したときなどに呼ぶ）
//   EditorGUI::BeginPopup(ID) 〜 EndPopup()        開いている間 true。中に MenuItem や部品を並べる
//   EditorGUI::BeginPopupModal(題名) 〜 EndPopup()  後ろを触れなくするダイアログ（OpenPopup(題名) で開く）
//   EditorGUI::BeginPopupContextItem(ID) 〜 EndPopup()    直前の部品を右クリックすると開く
//   EditorGUI::BeginPopupContextWindow(ID) 〜 EndPopup()  今の枠（ウィンドウか子の枠）の空いたところを右クリックすると開く
//   EditorGUI::CloseCurrentPopup()                 今のポップアップを閉じる
//   EditorGUI::MenuItem(文, ショートカットの表示, チェック, 押せるか)  選んだフレームだけ true
//   EditorGUI::BeginMenu(文, 押せるか) 〜 EndMenu()  入れ子のメニュー（カーソルを乗せると開く）
// ショートカットの文字は表示だけ。キーを受け取るのは EditorGUI::IsKeyPressed で別に書く。
class FeatureTestPopupTab
{
    FeatureTestChecklist checks;
    string lastAction = "（まだ何もしていません）";
    bool showGrid = true;
    string newName = "新しい名前";

    array<string> CheckItems()
    {
        array<string> items = {
            "「メニューを開く」でメニューが出て、選んだ項目が「最後の操作」に出る",
            "「格子を出す」は選ぶたびにチェックが付いたり外れたりする",
            "「押せない項目」と「開けないメニュー」は薄く、選べない",
            "「その他」にカーソルを乗せると入れ子のメニューが開く",
            "「ここを右クリック」のボタンを右クリックすると、そのボタン用のメニューが出る",
            "下の枠の空いたところを右クリックすると、枠のメニューが出る",
            "確認ダイアログは「はい」「いいえ」で閉じ、開いている間は後ろを押せない",
            "名前のダイアログで入れた名前が「最後の操作」に出る"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("メニュー");
        if (EditorGUI::Button("メニューを開く")) {
            EditorGUI::OpenPopup("menu");
        }
        if (EditorGUI::BeginPopup("menu")) {
            if (EditorGUI::MenuItem("複製", "Ctrl+D")) {
                lastAction = "複製";
            }
            if (EditorGUI::MenuItem("格子を出す", "", showGrid)) {
                showGrid = !showGrid;
                lastAction = showGrid ? "格子を出す" : "格子を隠す";
            }
            EditorGUI::MenuItem("押せない項目", "", false, false);
            EditorGUI::Separator();
            if (EditorGUI::BeginMenu("その他")) {
                if (EditorGUI::MenuItem("名前を変える")) {
                    lastAction = "名前を変える";
                }
                if (EditorGUI::BeginMenu("もっと奥")) {
                    if (EditorGUI::MenuItem("奥の項目")) {
                        lastAction = "奥の項目";
                    }
                }
                EditorGUI::EndMenu();
            }
            EditorGUI::EndMenu();
            if (EditorGUI::BeginMenu("開けないメニュー", false)) {
                EditorGUI::MenuItem("出ない項目");
            }
            EditorGUI::EndMenu();
        }
        EditorGUI::EndPopup();
        EditorGUI::Label("格子", showGrid ? "出す" : "隠す");

        EditorGUI::Header("右クリックのメニュー");
        EditorGUI::Button("ここを右クリック");
        if (EditorGUI::BeginPopupContextItem("button-menu")) {
            if (EditorGUI::MenuItem("ボタンの項目")) {
                lastAction = "ボタンの右クリックの項目";
            }
        }
        EditorGUI::EndPopup();

        if (EditorGUI::BeginChild("context-area", 0, 70)) {
            EditorGUI::TextDisabled("この枠の空いたところを右クリック");
            if (EditorGUI::BeginPopupContextWindow("area-menu")) {
                if (EditorGUI::MenuItem("枠の項目")) {
                    lastAction = "枠の右クリックの項目";
                }
            }
            EditorGUI::EndPopup();
        }
        EditorGUI::EndChild();

        EditorGUI::Header("ダイアログ");
        if (EditorGUI::Button("確認ダイアログ")) {
            EditorGUI::OpenPopup("確認");
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("名前のダイアログ")) {
            EditorGUI::OpenPopup("名前を入れる");
        }
        if (EditorGUI::BeginPopupModal("確認")) {
            EditorGUI::Label("本当に実行しますか？");
            if (EditorGUI::Button("はい", 90)) {
                lastAction = "はい";
                EditorGUI::CloseCurrentPopup();
            }
            EditorGUI::SameLine();
            if (EditorGUI::Button("いいえ", 90)) {
                lastAction = "いいえ";
                EditorGUI::CloseCurrentPopup();
            }
        }
        EditorGUI::EndPopup();
        if (EditorGUI::BeginPopupModal("名前を入れる")) {
            newName = EditorGUI::TextField("名前", newName);
            if (EditorGUI::Button("決める", 90)) {
                lastAction = "名前「" + newName + "」";
                EditorGUI::CloseCurrentPopup();
            }
            EditorGUI::SameLine();
            if (EditorGUI::Button("やめる", 90)) {
                EditorGUI::CloseCurrentPopup();
            }
        }
        EditorGUI::EndPopup();

        EditorGUI::Label("最後の操作", lastAction);
    }
}
