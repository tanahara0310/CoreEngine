// 「オブジェクト」のタブ。編集中のシーンのオブジェクトを作る・消す・探す・選ぶ・変える使い方
//
//   Editor::CreateObject(名前)・CreateObject(名前, 位置)   空のオブジェクトを作る
//   Editor::InstantiatePrefab(プレハブのパス, 名前)        プレハブから作る（読めなければ null）
//   Editor::DestroyObject(オブジェクト)                    消す
//   Editor::FindObject(名前)・GetObjects()                名前で探す・全部の一覧
//   Editor::GetSelection()・Select(オブジェクト)            選んでいるもの（無ければ null）・選ぶ（null で外す）
//   Editor::RecordObject(オブジェクト, 名前)                変える前に控える（Ctrl+Z で戻せる。変わらなければ何も積まない）
//   Editor::BeginUndoGroup(名前) 〜 EndUndoGroup()          間の操作を Ctrl+Z 1 回で戻るようにまとめる
//   Editor::isPlaying                                      再生中か（再生中に変えたものは停止で戻る）
//   GameObject の AddComponent・RemoveComponent・HasComponent(型名)・GetChildren()、Transform の SetParent・parent
// Editor:: で作ったものと消したもの、エディタの道具の中でのコンポーネントの付け外しは Ctrl+Z で戻せる。
// 位置や名前を変えるときは、変える前に RecordObject で控える。
class FeatureTestSceneTab
{
    FeatureTestChecklist checks;
    string prefab = "Application/Assets/Prefabs/Physics/PhysicsBox.prefab";
    string findName = "FeatureTest 空";
    string lastAction = "（まだ）";

    array<string> CheckItems()
    {
        array<string> items = {
            "「空のオブジェクトを作る」で Hierarchy に増え、Ctrl+Z で消え、Ctrl+Y で戻る",
            "「高さ 2 に作る」で、高さ 2 の位置にできる",
            "「プレハブから作る」で、プレハブの見た目のオブジェクトができる",
            "「親子をまとめて作る」の後、Ctrl+Z 1 回で親も子も消える",
            "選んだオブジェクトを「上へ 1 動かす」「名前に ★ を足す」「有効を切り替える」で変え、Ctrl+Z で 1 つずつ戻る",
            "「剛体を付け外し」でインスペクタに剛体が出たり消えたりし、Ctrl+Z で戻る",
            "「親から外す」で親子が外れ、Ctrl+Z で戻る",
            "「消す」で消え、Ctrl+Z で戻る",
            "名前で探すと見つかり、「選ぶ」でそのオブジェクトが選ばれる",
            "一覧の名前を押すと、そのオブジェクトが選ばれる",
            "「FeatureTest のオブジェクトを全部消す」で消え、Ctrl+Z 1 回で全部戻る",
            "再生中は「再生中」が はい になり、停止すると作ったものが消える"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());
        EditorGUI::Label("再生中", Editor::isPlaying ? "はい（停止すると、作ったものと変えたものは戻る）" : "いいえ");

        EditorGUI::Header("作る");
        if (EditorGUI::Button("空のオブジェクトを作る")) {
            Editor::Select(Editor::CreateObject("FeatureTest 空"));
            lastAction = "空のオブジェクトを作った";
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("高さ 2 に作る")) {
            Editor::Select(Editor::CreateObject("FeatureTest 高さ 2", Vector3(0.0f, 2.0f, 0.0f)));
            lastAction = "高さ 2 に作った";
        }
        prefab = EditorGUI::AssetField("プレハブ", prefab, "Prefab");
        if (EditorGUI::Button("プレハブから作る")) {
            GameObject@ created = Editor::InstantiatePrefab(prefab, "FeatureTest プレハブ");
            if (created is null) {
                Error("プレハブを読めません: " + prefab);
            } else {
                created.transform.position = Vector3(0.0f, 0.5f, 0.0f);
                Editor::Select(created);
                lastAction = "プレハブから作った";
            }
        }
        if (EditorGUI::Button("親子をまとめて作る")) {
            CreateFamily();
        }

        EditorGUI::Header("選んでいるオブジェクト");
        DrawSelection();

        EditorGUI::Header("探す");
        findName = EditorGUI::TextField("名前", findName);
        GameObject@ found = Editor::FindObject(findName);
        EditorGUI::Label("見つかったもの", found is null ? "（なし）" : found.name);
        EditorGUI::enabled = found !is null;
        if (EditorGUI::Button("選ぶ")) {
            Editor::Select(found);
        }
        EditorGUI::enabled = true;

        EditorGUI::Header("シーンのオブジェクト");
        DrawObjectList();

        EditorGUI::Header("後片付け");
        if (EditorGUI::Button("FeatureTest のオブジェクトを全部消す")) {
            RemoveTestObjects();
        }
        EditorGUI::Label("最後の操作", lastAction);
    }

    private void CreateFamily()
    {
        Editor::BeginUndoGroup("親子をまとめて作る");
        GameObject@ parent = Editor::CreateObject("FeatureTest 親", Vector3(0.0f, 1.0f, 0.0f));
        for (int i = 0; i < 3; ++i) {
            GameObject@ child = Editor::CreateObject("子 " + (i + 1), Vector3(1.5f * (i - 1), 0.0f, 0.0f));
            child.transform.SetParent(parent.transform);
            child.transform.UpdateMatrix();
        }
        Editor::EndUndoGroup();
        Editor::Select(parent);
        lastAction = "親子をまとめて作った";
    }

    private void DrawSelection()
    {
        GameObject@ selected = Editor::GetSelection();
        if (selected is null) {
            EditorGUI::TextDisabled("Hierarchy かシーンビューでオブジェクトを選んでください");
            return;
        }
        Transform@ parent = selected.transform.parent;
        Vector3 position = selected.transform.position;
        EditorGUI::Label("名前", selected.name);
        EditorGUI::Label("有効", selected.active ? "はい" : "いいえ");
        EditorGUI::Label("親", parent is null ? "（なし）" : parent.gameObject.name);
        EditorGUI::Label("子の数", "" + selected.GetChildren().length());
        EditorGUI::Label("位置", formatFloat(position.x, "", 0, 2) + ", " + formatFloat(position.y, "", 0, 2) + ", " + formatFloat(position.z, "", 0, 2));
        EditorGUI::Label("剛体", selected.HasComponent("Rigidbody") ? "あり" : "なし");

        if (EditorGUI::Button("上へ 1 動かす")) {
            Editor::RecordObject(selected, selected.name + " を上へ動かす");
            selected.transform.position = position + Vector3(0.0f, 1.0f, 0.0f);
            lastAction = "上へ 1 動かした";
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("名前に ★ を足す")) {
            Editor::RecordObject(selected, "名前に ★ を足す");
            selected.name = selected.name + "★";
            lastAction = "名前に ★ を足した";
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("有効を切り替える")) {
            Editor::RecordObject(selected, "有効を切り替える");
            selected.active = !selected.active;
            lastAction = "有効を切り替えた";
        }
        if (EditorGUI::Button("剛体を付け外し")) {
            if (selected.HasComponent("Rigidbody")) {
                selected.RemoveComponent("Rigidbody");
                lastAction = "剛体を外した";
            } else {
                selected.AddComponent("Rigidbody");
                lastAction = "剛体を付けた";
            }
        }
        EditorGUI::SameLine();
        EditorGUI::enabled = parent !is null;
        if (EditorGUI::Button("親から外す")) {
            Editor::RecordObject(selected, "親から外す");
            selected.transform.SetParent(null);
            selected.transform.UpdateMatrix();
            lastAction = "親から外した";
        }
        EditorGUI::enabled = true;
        EditorGUI::SameLine();
        if (EditorGUI::Button("消す")) {
            Editor::DestroyObject(selected);
            lastAction = "消した";
        }
    }

    private void DrawObjectList()
    {
        array<GameObject@>@ objects = Editor::GetObjects();
        GameObject@ selected = Editor::GetSelection();
        EditorGUI::Label("数", "" + objects.length());
        if (EditorGUI::BeginChild("objects", 0, 140)) {
            for (uint i = 0; i < objects.length() && i < 300; ++i) {
                EditorGUI::PushID(int(i));
                bool isSelected = selected !is null && selected == objects[i];
                if (EditorGUI::Selectable(objects[i].name, isSelected)) {
                    Editor::Select(objects[i]);
                }
                EditorGUI::PopID();
            }
        }
        EditorGUI::EndChild();
    }

    private void RemoveTestObjects()
    {
        array<GameObject@>@ objects = Editor::GetObjects();
        int removed = 0;
        Editor::BeginUndoGroup("FeatureTest のオブジェクトを消す");
        for (uint i = 0; i < objects.length(); ++i) {
            if (objects[i].isAlive && objects[i].name.findFirst("FeatureTest") == 0) {
                Editor::DestroyObject(objects[i]);
                ++removed;
            }
        }
        Editor::EndUndoGroup();
        lastAction = "FeatureTest のオブジェクトを " + removed + " 個消した";
    }
}
