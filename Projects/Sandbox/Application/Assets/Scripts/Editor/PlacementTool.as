// シーンビューをクリックしてプレハブを置いていくウィンドウ。Tools > 配置ツール で開く。
// 「クリックで置く」をオンにすると、カーソルの下（物の表面か、決めた高さの床）に置く場所を描き、左クリックで置く。
// Shift を押しながらクリックすると、カーソルの下の置いた物を消す。どちらも Ctrl+Z で戻せる。
[MenuItem("Tools/配置ツール")]
class PlacementTool : EditorWindow
{
    string prefab = "Application/Assets/Prefabs/MapGenerator/MapWall.prefab";
    bool placing = false;
    bool onSurface = true;
    float floorHeight = 0.0f;
    bool snapToGrid = true;
    float gridSize = 1.0f;
    string rootName = "PlacedObjects";

    void OnGUI()
    {
        prefab = EditorGUI::AssetField("置くプレハブ", prefab, "Prefab");
        placing = EditorGUI::Toggle("クリックで置く", placing);
        EditorGUI::Tooltip("オンの間は、シーンビューのクリックでオブジェクトを選ばない");
        onSurface = EditorGUI::Toggle("物の上に置く", onSurface);
        EditorGUI::Tooltip("オフなら、いつも床の高さに置く（カーソルの下に物が無いときも床に置く）");
        floorHeight = EditorGUI::FloatField("床の高さ", floorHeight);
        snapToGrid = EditorGUI::Toggle("格子に合わせる", snapToGrid);
        EditorGUI::enabled = snapToGrid;
        gridSize = Max(0.1f, EditorGUI::FloatField("格子の大きさ", gridSize));
        EditorGUI::enabled = true;
        rootName = EditorGUI::TextField("まとめるオブジェクト", rootName);

        GameObject@ root = Editor::FindObject(rootName);
        int count = root is null ? 0 : int(root.GetChildren().length());
        EditorGUI::Label("置いた数", "" + count);
        EditorGUI::HelpBox("シーンビューで左クリックすると置き、Shift を押しながらクリックすると置いた物を消します。どちらも Ctrl+Z で戻せます。");
    }

    void OnSceneGUI()
    {
        if (!placing) {
            return;
        }
        // 置いている間は、シーンビューのクリックでオブジェクトを選ばない
        SceneView::UseMouse();

        if (SceneView::shift) {
            GameObject@ target = PlacedUnderMouse();
            if (target !is null) {
                Handles::DrawWireSphere(target.transform.worldPosition, gridSize * 0.6f, Vector4(1.0f, 0.4f, 0.3f, 1.0f));
                if (SceneView::IsMouseClicked(MouseButton::Left)) {
                    Editor::DestroyObject(target);
                }
            }
            return;
        }

        Vector3 center;
        Vector3 point;
        if (!FindPlace(center, point)) {
            return;
        }
        Vector4 color = Vector4(0.4f, 0.9f, 1.0f, 1.0f);
        DrawGrid(Vector3(center.x, center.y - gridSize * 0.5f, center.z));
        Handles::DrawWireCube(center, Vector3(gridSize, gridSize, gridSize), color);
        Handles::DrawDot(point, 6.0f, color);
        Handles::Label(center, "(" + center.x + ", " + center.y + ", " + center.z + ")", color);
        if (SceneView::IsMouseClicked(MouseButton::Left)) {
            Place(center);
        }
    }

    // 置く箱の中心と、カーソルが指している点を求める
    private bool FindPlace(Vector3 &out center, Vector3 &out point)
    {
        Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
        center = Vector3(0.0f, 0.0f, 0.0f);
        if (!(onSurface && SceneView::RaycastMouse(point, normal))) {
            normal = Vector3(0.0f, 1.0f, 0.0f);
            if (!SceneView::MouseToPlane(floorHeight, point)) {
                return false;
            }
        }
        // 指した面の外側に、箱の半分だけ浮かせる
        center = point + normal * (gridSize * 0.5f);
        if (snapToGrid) {
            center.x = floor(center.x / gridSize + 0.5f) * gridSize;
            center.z = floor(center.z / gridSize + 0.5f) * gridSize;
            center.y = floor((center.y - floorHeight) / gridSize) * gridSize + gridSize * 0.5f + floorHeight;
        }
        return true;
    }

    // 置く場所の周りに薄い格子を描く
    private void DrawGrid(const Vector3 &in bottom)
    {
        Vector4 color = Vector4(0.4f, 0.9f, 1.0f, 0.3f);
        float half = gridSize * 2.5f;
        for (int i = 0; i <= 5; ++i) {
            float offset = -half + gridSize * i;
            Handles::DrawLine(Vector3(bottom.x + offset, bottom.y, bottom.z - half),
                Vector3(bottom.x + offset, bottom.y, bottom.z + half), color, 1.0f);
            Handles::DrawLine(Vector3(bottom.x - half, bottom.y, bottom.z + offset),
                Vector3(bottom.x + half, bottom.y, bottom.z + offset), color, 1.0f);
        }
    }

    private void Place(const Vector3 &in center)
    {
        Editor::BeginUndoGroup("配置ツールで置く");
        GameObject@ root = Editor::FindObject(rootName);
        if (root is null) {
            @root = Editor::CreateObject(rootName);
        }
        GameObject@ placed = Editor::InstantiatePrefab(prefab, "Placed");
        if (placed is null) {
            Error("置くプレハブを読めません: " + prefab);
            Editor::EndUndoGroup();
            return;
        }
        placed.transform.position = center;
        placed.transform.scale = Vector3(gridSize, gridSize, gridSize);
        placed.transform.SetParent(root.transform);
        placed.transform.UpdateMatrix();
        Editor::EndUndoGroup();
    }

    // カーソルの下の、このツールで置いた物（まとめるオブジェクトの子）
    private GameObject@ PlacedUnderMouse()
    {
        GameObject@ object = SceneView::GetObjectUnderMouse();
        if (object is null) {
            return null;
        }
        Transform@ parent = object.transform.parent;
        if (parent is null || parent.gameObject.name != rootName) {
            return null;
        }
        return object;
    }
}
