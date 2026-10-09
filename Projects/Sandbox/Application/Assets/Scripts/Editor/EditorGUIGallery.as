// EditorGUI の部品をひととおり並べた見本のウィンドウ。Tools > 部品の見本 で開く
[MenuItem("Tools/部品の見本")]
class EditorGUIGallery : EditorWindow
{
    bool toggle = true;
    int number = 3;
    float ratio = 0.5f;
    string text = "こんにちは";
    string notes = "複数行の\nメモ";
    Vector3 position = Vector3(0.0f, 1.0f, 0.0f);
    Vector4 color = Vector4(0.9f, 0.6f, 0.2f, 1.0f);
    int mode = 0;
    int changes = 0;

    array<string> fruits = { "りんご", "みかん", "ぶどう", "もも", "いちご" };
    int fruit = 0;

    array<string> enemyNames = { "スライム", "ゴブリン", "ドラゴン", "コウモリ" };
    array<int> enemyHp = { 10, 30, 500, 8 };
    int selectedEnemy = -1;

    int selectedNode = -1;
    string lastAction = "（まだ何もしていません）";
    string lastKey = "（まだ押していません）";

    void OnGUI()
    {
        if (EditorGUI::BeginTabBar("tabs")) {
            if (EditorGUI::BeginTabItem("文字")) {
                DrawTextTab();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("入力")) {
                DrawInputTab();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("一覧と表")) {
                DrawListTab();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("木")) {
                DrawTreeTab();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("ポップアップ")) {
                DrawPopupTab();
            }
            EditorGUI::EndTabItem();
            if (EditorGUI::BeginTabItem("マウスとキー")) {
                DrawDeviceTab();
            }
            EditorGUI::EndTabItem();
        }
        EditorGUI::EndTabBar();
    }

    private void DrawTextTab()
    {
        EditorGUI::Header("文字");
        EditorGUI::Label("ふつうの文字。長い文はウィンドウの幅で折り返します。");
        EditorGUI::TextColored("色付きの文字", Vector4(0.95f, 0.55f, 0.2f, 1.0f));
        EditorGUI::TextDisabled("薄い文字（補足に使う）");
        EditorGUI::BulletText("箇条書きの 1 つ目");
        EditorGUI::BulletText("箇条書きの 2 つ目");
        EditorGUI::Label("ラベル", "値");
        EditorGUI::HelpBox("説明の囲み");
        EditorGUI::HelpBox("警告の囲み", EditorGUI::MessageType::Warning);
        EditorGUI::Button("ツールチップ付きのボタン");
        EditorGUI::Tooltip("カーソルを乗せると出る説明です");

        EditorGUI::Header("進み具合");
        EditorGUI::ProgressBar(ratio, formatInt(int(ratio * 100.0f)) + "%");
    }

    private void DrawInputTab()
    {
        EditorGUI::changed = false;
        toggle = EditorGUI::Toggle("チェック", toggle);
        number = EditorGUI::IntField("整数", number);
        ratio = EditorGUI::Slider("割合", ratio, 0.0f, 1.0f);
        text = EditorGUI::TextField("文字", text);
        notes = EditorGUI::TextArea("複数行", notes);
        position = EditorGUI::Vector3Field("位置", position);
        color = EditorGUI::ColorField("色", color);
        if (EditorGUI::changed) {
            ++changes;
        }
        EditorGUI::Label("値が変わったフレーム", "" + changes);

        EditorGUI::Header("ラジオボタン");
        mode = EditorGUI::RadioButton("歩く", mode, 0);
        EditorGUI::SameLine();
        mode = EditorGUI::RadioButton("走る", mode, 1);
        EditorGUI::SameLine();
        mode = EditorGUI::RadioButton("飛ぶ", mode, 2);

        EditorGUI::Header("押せなくする");
        EditorGUI::enabled = toggle;
        EditorGUI::Button("チェックが入っているときだけ押せる");
        EditorGUI::enabled = true;
    }

    private void DrawListTab()
    {
        EditorGUI::Header("一覧");
        fruit = EditorGUI::ListBox("果物", fruit, fruits, 4);

        EditorGUI::Header("表（見出しを押すと並べ替え）");
        if (EditorGUI::BeginTable("enemies", 2, EditorGUI::TableFlags::Default | EditorGUI::TableFlags::Sortable)) {
            EditorGUI::TableSetupColumn("名前");
            EditorGUI::TableSetupColumn("HP", 90);
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
                EditorGUI::PopID();
            }
        }
        EditorGUI::EndTable();

        EditorGUI::Header("スクロールする枠");
        if (EditorGUI::BeginChild("scroll", 0, 110)) {
            for (int i = 1; i <= 30; ++i) {
                EditorGUI::Label("行 " + i);
            }
        }
        EditorGUI::EndChild();
    }

    private void DrawTreeTab()
    {
        EditorGUI::TextDisabled("矢印かダブルクリックで開閉し、押すと選びます");
        bool open = EditorGUI::BeginTreeNode("ステージ 1", true, selectedNode == 0);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = 0;
        }
        if (open) {
            DrawLeaf("敵の配置", 1);
            DrawLeaf("アイテム", 2);
        }
        EditorGUI::EndTreeNode();

        open = EditorGUI::BeginTreeNode("ステージ 2", false, selectedNode == 3);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = 3;
        }
        if (open) {
            DrawLeaf("ボス", 4);
        }
        EditorGUI::EndTreeNode();

        EditorGUI::Label("選んでいる番号", "" + selectedNode);
    }

    private void DrawLeaf(const string &in label, int id)
    {
        EditorGUI::BeginTreeNode(label, false, selectedNode == id, true);
        if (EditorGUI::IsItemClicked()) {
            selectedNode = id;
        }
        EditorGUI::EndTreeNode();
    }

    private void DrawPopupTab()
    {
        if (EditorGUI::Button("メニューを開く")) {
            EditorGUI::OpenPopup("menu");
        }
        if (EditorGUI::BeginPopup("menu")) {
            if (EditorGUI::MenuItem("複製", "Ctrl+D")) {
                lastAction = "複製";
            }
            if (EditorGUI::MenuItem("削除")) {
                lastAction = "削除";
            }
            if (EditorGUI::BeginMenu("その他")) {
                if (EditorGUI::MenuItem("名前を変える")) {
                    lastAction = "名前を変える";
                }
            }
            EditorGUI::EndMenu();
        }
        EditorGUI::EndPopup();

        EditorGUI::Button("ここを右クリック");
        if (EditorGUI::BeginPopupContextItem()) {
            if (EditorGUI::MenuItem("右クリックの項目")) {
                lastAction = "右クリックの項目";
            }
        }
        EditorGUI::EndPopup();

        if (EditorGUI::Button("確認ダイアログ")) {
            EditorGUI::OpenPopup("確認");
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

        EditorGUI::Label("最後の操作", lastAction);
    }

    private void DrawDeviceTab()
    {
        EditorGUI::TextDisabled("このウィンドウを選んでから、Delete・F2・Space を押してください");
        if (EditorGUI::IsKeyPressed(Key::Delete)) {
            lastKey = "Delete";
        }
        if (EditorGUI::IsKeyPressed(Key::F2)) {
            lastKey = "F2";
        }
        if (EditorGUI::IsKeyPressed(Key::Space)) {
            lastKey = "Space";
        }
        EditorGUI::Label("最後に押したキー", lastKey);
        EditorGUI::Label("修飾キー", (EditorGUI::ctrl ? "Ctrl " : "") + (EditorGUI::shift ? "Shift " : "") + (EditorGUI::alt ? "Alt" : ""));

        EditorGUI::Header("描く場所（ドラッグで線・ホイールの量を表示）");
        EditorGUI::Canvas(0, 140);
        if (EditorGUI::IsCanvasHovered()) {
            Vector2 mouse = EditorGUI::GetCanvasMousePosition();
            EditorGUI::DrawText(Vector2(8.0f, 8.0f), "ホイール " + EditorGUI::GetMouseWheel(), Vector4(0.9f, 0.9f, 0.9f, 1.0f));
            if (EditorGUI::IsMouseDragging()) {
                Vector2 delta = EditorGUI::GetMouseDragDelta();
                EditorGUI::DrawLine(mouse - delta, mouse, Vector4(0.95f, 0.55f, 0.2f, 1.0f), 2.0f);
            }
            if (EditorGUI::IsMouseDoubleClicked()) {
                lastAction = "描く場所をダブルクリック";
            }
        }
    }

    private void SortEnemies(int column, bool ascending)
    {
        for (uint i = 0; i < enemyNames.length(); ++i) {
            for (uint j = i + 1; j < enemyNames.length(); ++j) {
                const int order = Compare(column, i, j);
                if (ascending ? order > 0 : order < 0) {
                    const string name = enemyNames[i];
                    enemyNames[i] = enemyNames[j];
                    enemyNames[j] = name;
                    const int hp = enemyHp[i];
                    enemyHp[i] = enemyHp[j];
                    enemyHp[j] = hp;
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
        if (enemyNames[a] == enemyNames[b]) {
            return 0;
        }
        return enemyNames[a] < enemyNames[b] ? -1 : 1;
    }
}
