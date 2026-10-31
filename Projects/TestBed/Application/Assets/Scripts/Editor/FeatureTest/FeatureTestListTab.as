using namespace CoreEngine;

// 「一覧」のタブ。選ぶ部品・表・木の使い方
//
//   EditorGUI::Selectable(文, 選んでいるか, 行全体)  押したフレームだけ true（表の中で行全体を押せるようにするなら 3 つ目を true）
//   EditorGUI::ListBox(ラベル, 選んだ番号, 項目, 見える行数)   文字の配列から 1 つ選ぶ一覧
//   EditorGUI::BeginListBox(ラベル, 高さ) 〜 EndListBox()   中に Selectable を並べる一覧（複数選びたいときなど）
//   EditorGUI::Popup(ラベル, 選んだ番号, 項目)              押すと開く選択肢
//   EditorGUI::BeginTable(ID, 列の数, TableFlags, 高さ) 〜 EndTable()
//       TableFlags は Borders（罫線）・RowBackground（縞）・Resizable（列の幅を変えられる）・Sortable（見出しで並べ替え）・
//       ScrollY（高さを決めてスクロール）。Default は罫線と縞と幅の変更
//     TableSetupColumn(見出し, 幅)  TableSetupScrollFreeze(列, 行)  TableHeadersRow()
//     TableNextRow()  TableNextColumn()  TableSetColumnIndex(列)
//     IsTableSortChanged()  GetTableSortColumn()  IsTableSortAscending()   見出しを押したら並べ替える
//   EditorGUI::BeginTreeNode(文, 最初は開くか, 選んでいるか, 葉か) 〜 EndTreeNode()  開いていれば true
// 入れ子のタブ（BeginTabBar の中の BeginTabBar）は、このウィンドウの上の段と下の段がそれに当たる。
class FeatureTestListTab
{
    FeatureTestChecklist checks;
    array<string> fruits = { "りんご", "みかん", "ぶどう", "もも", "いちご" };
    int fruit = 0;
    array<string> belongings = { "剣", "盾", "弓", "杖", "薬", "鍵" };
    array<bool> itemSelected = { false, false, false, false, false, false };
    array<string> levels = { "やさしい", "ふつう", "むずかしい" };
    int level = 1;

    array<string> enemyNames = { "スライム", "ゴブリン", "ドラゴン", "コウモリ", "ゴーレム", "おばけ", "オオカミ", "魔法使い" };
    array<int> enemyHp = { 10, 30, 500, 8, 120, 25, 40, 60 };
    array<float> enemySpeed = { 1.0f, 2.5f, 1.5f, 4.0f, 0.5f, 3.0f, 5.0f, 2.0f };
    int selectedEnemy = -1;

    int selectedNode = -1;

    array<string> CheckItems()
    {
        array<string> items = {
            "一覧で果物を選ぶと「選んだ果物」が変わる",
            "持ち物の一覧は、押すと 1 つだけ選び、Ctrl を押しながら押すと複数選べる",
            "難しさを押すと選択肢が開き、選ぶと変わる",
            "表の見出し（名前・HP・速さ）を押すと並べ替わり、もう一度押すと逆順になる",
            "表をスクロールしても、見出しの行と名前の列が残る",
            "表の列の境目をドラッグすると幅が変わる",
            "表の名前を押すと行全体が選ばれ、HP の欄を直せる",
            "木の矢印（かダブルクリック）で開閉し、押した項目の番号が出る"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("一覧");
        fruit = EditorGUI::ListBox("果物", fruit, fruits, 4);
        EditorGUI::Label("選んだ果物", fruits[fruit]);

        if (EditorGUI::BeginListBox("持ち物", 110)) {
            for (uint i = 0; i < belongings.length(); ++i) {
                if (EditorGUI::Selectable(belongings[i], itemSelected[i])) {
                    if (EditorGUI::ctrl) {
                        itemSelected[i] = !itemSelected[i];
                    } else {
                        for (uint j = 0; j < itemSelected.length(); ++j) {
                            itemSelected[j] = false;
                        }
                        itemSelected[i] = true;
                    }
                }
            }
        }
        EditorGUI::EndListBox();
        EditorGUI::Label("選んだ持ち物", SelectedItems());

        level = EditorGUI::Popup("難しさ", level, levels);

        EditorGUI::Header("表（見出しを押すと並べ替え）");
        int flags = EditorGUI::TableFlags::Default | EditorGUI::TableFlags::Sortable | EditorGUI::TableFlags::ScrollY;
        if (EditorGUI::BeginTable("enemies", 3, flags, 150)) {
            EditorGUI::TableSetupColumn("名前");
            EditorGUI::TableSetupColumn("HP", 90);
            EditorGUI::TableSetupColumn("速さ", 70);
            // 1 列目と見出しの行を、スクロールしても残す
            EditorGUI::TableSetupScrollFreeze(1, 1);
            EditorGUI::TableHeadersRow();
            if (EditorGUI::IsTableSortChanged()) {
                SortEnemies(EditorGUI::GetTableSortColumn(), EditorGUI::IsTableSortAscending());
            }
            for (uint i = 0; i < enemyNames.length(); ++i) {
                EditorGUI::PushID(int(i));
                EditorGUI::TableNextRow();
                EditorGUI::TableNextColumn();
                if (EditorGUI::Selectable(enemyNames[i], selectedEnemy == int(i), true)) {
                    selectedEnemy = int(i);
                }
                EditorGUI::TableNextColumn();
                enemyHp[i] = EditorGUI::IntField("##hp", enemyHp[i]);
                // 列の番号を決めて書くこともできる
                EditorGUI::TableSetColumnIndex(2);
                EditorGUI::Label(formatFloat(enemySpeed[i], "", 0, 1));
                EditorGUI::PopID();
            }
        }
        EditorGUI::EndTable();
        EditorGUI::Label("選んだ敵", selectedEnemy < 0 ? "（なし）" : enemyNames[selectedEnemy] + "（HP " + enemyHp[selectedEnemy] + "）");

        EditorGUI::Header("木");
        bool open = EditorGUI::BeginTreeNode("ステージ 1", true, selectedNode == 0);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = 0;
        }
        if (open) {
            DrawLeaf("敵の配置", 1);
            DrawLeaf("アイテム", 2);
            bool inner = EditorGUI::BeginTreeNode("入れ子", false, selectedNode == 3);
            if (EditorGUI::IsItemClicked()) {
                selectedNode = 3;
            }
            if (inner) {
                DrawLeaf("奥の葉", 4);
            }
            EditorGUI::EndTreeNode();
        }
        EditorGUI::EndTreeNode();

        open = EditorGUI::BeginTreeNode("ステージ 2", false, selectedNode == 5);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = 5;
        }
        if (open) {
            DrawLeaf("ボス", 6);
        }
        EditorGUI::EndTreeNode();
        EditorGUI::Label("選んでいる番号", selectedNode < 0 ? "（なし）" : "" + selectedNode);
    }

    private void DrawLeaf(const string &in label, int id)
    {
        EditorGUI::BeginTreeNode(label, false, selectedNode == id, true);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = id;
        }
        EditorGUI::EndTreeNode();
    }

    private string SelectedItems()
    {
        string names = "";
        for (uint i = 0; i < belongings.length(); ++i) {
            if (itemSelected[i]) {
                names += (names == "" ? "" : "・") + belongings[i];
            }
        }
        return names == "" ? "（なし）" : names;
    }

    private void SortEnemies(int column, bool ascending)
    {
        for (uint i = 0; i < enemyNames.length(); ++i) {
            for (uint j = i + 1; j < enemyNames.length(); ++j) {
                const int order = Compare(column, i, j);
                if (ascending ? order > 0 : order < 0) {
                    Swap(i, j);
                }
            }
        }
        selectedEnemy = -1;
    }

    private int Compare(int column, uint a, uint b)
    {
        if (column == 1) {
            return enemyHp[a] - enemyHp[b];
        }
        if (column == 2) {
            return enemySpeed[a] < enemySpeed[b] ? -1 : enemySpeed[a] > enemySpeed[b] ? 1 : 0;
        }
        if (enemyNames[a] == enemyNames[b]) {
            return 0;
        }
        return enemyNames[a] < enemyNames[b] ? -1 : 1;
    }

    private void Swap(uint a, uint b)
    {
        const string name = enemyNames[a];
        enemyNames[a] = enemyNames[b];
        enemyNames[b] = name;
        const int hp = enemyHp[a];
        enemyHp[a] = enemyHp[b];
        enemyHp[b] = hp;
        const float speed = enemySpeed[a];
        enemySpeed[a] = enemySpeed[b];
        enemySpeed[b] = speed;
    }
}
