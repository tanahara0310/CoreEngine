// メニューから 1 回呼ぶエディタの道具。クラスの外の関数に [MenuItem] を付ける

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
