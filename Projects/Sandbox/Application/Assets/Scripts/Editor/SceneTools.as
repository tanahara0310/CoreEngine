// メニューから 1 回呼ぶエディタの道具。クラスの外の関数に [MenuItem] を付ける

// 選んでいるオブジェクトを地面（高さ 0）まで下ろす（RecordObject で控えたので Ctrl+Z で戻せる）
[MenuItem("Tools/選んだオブジェクトを地面に下ろす")]
void DropSelectionToGround()
{
    GameObject@ selected = Editor::GetSelection();
    if (selected is null) {
        Warn("オブジェクトを選んでから実行してください");
        return;
    }
    Editor::RecordObject(selected, selected.name + " を地面に下ろす");
    Vector3 position = selected.transform.position;
    position.y = 0.0f;
    selected.transform.position = position;
}

// 選んでいるオブジェクトに剛体を付ける（付いていれば外す。Ctrl+Z で戻せる）
[MenuItem("Tools/選んだオブジェクトの剛体を付け外し")]
void ToggleRigidbodyOnSelection()
{
    GameObject@ selected = Editor::GetSelection();
    if (selected is null) {
        Warn("オブジェクトを選んでから実行してください");
        return;
    }
    if (selected.HasComponent("Rigidbody")) {
        selected.RemoveComponent("Rigidbody");
    } else {
        selected.AddComponent("Rigidbody");
    }
}

// 開いているシーンのオブジェクトを数えて Console へ出す
[MenuItem("Tools/シーンのオブジェクトを数える")]
void CountSceneObjects()
{
    array<GameObject@>@ objects = Editor::GetObjects();
    int active = 0;
    for (uint i = 0; i < objects.length(); ++i) {
        if (objects[i].active) {
            ++active;
        }
    }
    Log("シーンのオブジェクト: " + objects.length() + " 個（有効 " + active + " 個）");
}
