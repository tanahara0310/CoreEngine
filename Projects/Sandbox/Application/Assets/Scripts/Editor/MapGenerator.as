// 洞窟のような地形をセル・オートマトンで作り、壁のブロックとしてシーンへ並べるエディタのウィンドウ。
// Tools > マップ生成 で開く。プレビューは左ドラッグで壁、右ドラッグで床に塗れる。
// 地形はデータファイル（Application/Assets の JSON）へ保存でき、ゲームのスクリプトから DataFile で読める。
// 公開メンバ変数の値は、エディタを閉じても次の起動で戻る。
[MenuItem("Tools/マップ生成")]
class MapGenerator : EditorWindow
{
    int width = 24;
    int depth = 16;
    float wallRate = 0.45f;
    int smoothSteps = 4;
    int seed = 1;
    float cellSize = 1.0f;
    float wallHeight = 1.5f;
    string wallPrefab = "Application/Assets/Prefabs/MapGenerator/MapWall.prefab";
    string rootName = "GeneratedMap";
    string dataPath = "Data/Maps/Cave.json";

    // マスごとの壁（1）と床（0）。上の行が奥
    array<int> cells;
    int cellsWidth = 0;
    int cellsDepth = 0;

    private string fileMessage_ = "";

    void OnEnable()
    {
        if (cellsWidth != width || cellsDepth != depth) {
            Generate();
        }
    }

    void OnGUI()
    {
        EditorGUI::Header("大きさ");
        width = EditorGUI::IntSlider("幅", width, 8, 64);
        depth = EditorGUI::IntSlider("奥行き", depth, 8, 64);

        EditorGUI::Header("生成");
        wallRate = EditorGUI::Slider("壁の割合", wallRate, 0.3f, 0.6f);
        EditorGUI::Tooltip("外周の内側のマスを、最初に壁にする割合");
        smoothSteps = EditorGUI::IntSlider("ならす回数", smoothSteps, 0, 8);
        EditorGUI::Tooltip("周りの 8 マスで壁か床かを決め直す回数（多いほど洞窟がなめらかになる）");
        seed = EditorGUI::IntField("シード", seed);
        EditorGUI::Tooltip("同じシードなら毎回同じ地形になる");
        if (EditorGUI::Button("生成")) {
            Generate();
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("シードを変えて生成")) {
            seed = Random::Range(1, 99999);
            Generate();
        }
        if (cellsWidth != width || cellsDepth != depth) {
            Generate();
        }

        EditorGUI::Header("プレビュー");
        DrawPreview();
        EditorGUI::Label("左ドラッグで壁、右ドラッグで床に塗れます（壁 " + CountWalls() + " 個）");

        EditorGUI::Header("シーンへ配置");
        cellSize = EditorGUI::FloatField("1 マスの大きさ", cellSize);
        wallHeight = EditorGUI::FloatField("壁の高さ", wallHeight);
        wallPrefab = EditorGUI::AssetField("壁のプレハブ", wallPrefab, "Prefab");
        rootName = EditorGUI::TextField("まとめるオブジェクト", rootName);
        if (EditorGUI::Button("シーンに配置", -1)) {
            PlaceInScene();
        }
        GameObject@ placed = Editor::FindObject(rootName);
        EditorGUI::enabled = placed !is null;
        if (EditorGUI::Button("配置したものを消す", -1)) {
            EditorGUI::OpenPopup("消去の確認");
        }
        EditorGUI::enabled = true;
        if (EditorGUI::BeginPopupModal("消去の確認")) {
            EditorGUI::Label(rootName + " とその子をすべて消します。");
            if (EditorGUI::Button("消す", 100)) {
                if (placed !is null) {
                    Editor::DestroyObject(placed);
                }
                EditorGUI::CloseCurrentPopup();
            }
            EditorGUI::SameLine();
            if (EditorGUI::Button("やめる", 100)) {
                EditorGUI::CloseCurrentPopup();
            }
        }
        EditorGUI::EndPopup();
        EditorGUI::HelpBox("配置と消去は Ctrl+Z で戻せます。残すときはシーンを保存（Ctrl+S）してください。");

        EditorGUI::Header("データファイル");
        dataPath = EditorGUI::TextField("保存先", dataPath);
        EditorGUI::Tooltip("Application/Assets からの相対パス（.json）");
        if (EditorGUI::Button("ファイルに保存")) {
            SaveMap();
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("ファイルから読み込む")) {
            LoadMap();
        }
        if (fileMessage_ != "") {
            EditorGUI::TextDisabled(fileMessage_);
        }
    }

    // 大きさ・シード・マスをデータファイルへ書く
    private void SaveMap()
    {
        DataFile@ file = DataFile(dataPath);
        file.SetInt("width", width);
        file.SetInt("depth", depth);
        file.SetInt("seed", seed);
        file.SetIntArray("cells", cells);
        fileMessage_ = file.Save() ? "保存しました: " + dataPath : "保存できませんでした: " + dataPath;
    }

    // データファイルから大きさとマスを読む
    private void LoadMap()
    {
        DataFile@ file = DataFile(dataPath);
        if (!file.exists) {
            fileMessage_ = "ファイルがありません: " + dataPath;
            return;
        }
        array<int>@ loaded = file.GetIntArray("cells");
        int loadedWidth = file.GetInt("width", width);
        int loadedDepth = file.GetInt("depth", depth);
        if (loaded.length() != uint(loadedWidth * loadedDepth)) {
            fileMessage_ = "マスの数が大きさと合いません: " + dataPath;
            return;
        }
        width = loadedWidth;
        depth = loadedDepth;
        seed = file.GetInt("seed", seed);
        cells = loaded;
        cellsWidth = width;
        cellsDepth = depth;
        fileMessage_ = "読み込みました: " + dataPath;
    }

    // マスを色で並べ、カーソルの下のマスを塗る
    private void DrawPreview()
    {
        float cell = floor(Clamp(EditorGUI::GetAvailableWidth() / width, 4.0f, 20.0f));
        EditorGUI::Canvas(cell * width, cell * depth);

        int hoverX = -1;
        int hoverRow = -1;
        if (EditorGUI::IsCanvasHovered()) {
            Vector2 mouse = EditorGUI::GetCanvasMousePosition();
            hoverX = int(floor(mouse.x / cell));
            hoverRow = int(floor(mouse.y / cell));
            if (IsInside(hoverX, hoverRow)) {
                if (EditorGUI::IsMouseDown(MouseButton::Left)) {
                    cells[hoverRow * width + hoverX] = 1;
                } else if (EditorGUI::IsMouseDown(MouseButton::Right)) {
                    cells[hoverRow * width + hoverX] = 0;
                }
            }
        }

        Vector4 wallColor = Vector4(0.27f, 0.29f, 0.35f, 1.0f);
        Vector4 floorColor = Vector4(0.86f, 0.8f, 0.64f, 1.0f);
        for (int row = 0; row < depth; ++row) {
            for (int x = 0; x < width; ++x) {
                Vector2 min = Vector2(x * cell, row * cell);
                Vector2 max = Vector2((x + 1) * cell - 1.0f, (row + 1) * cell - 1.0f);
                EditorGUI::DrawRect(min, max, cells[row * width + x] == 1 ? wallColor : floorColor);
            }
        }
        if (IsInside(hoverX, hoverRow)) {
            EditorGUI::DrawRectOutline(Vector2(hoverX * cell, hoverRow * cell),
                Vector2((hoverX + 1) * cell, (hoverRow + 1) * cell), Vector4(1.0f, 0.6f, 0.2f, 1.0f), 2.0f);
        }
    }

    // 外周を壁にし、中を壁の割合で散らしてから、ならす
    private void Generate()
    {
        cellsWidth = width;
        cellsDepth = depth;
        cells.resize(uint(width * depth));
        RandomStream@ random = RandomStream(uint(seed));
        for (int row = 0; row < depth; ++row) {
            for (int x = 0; x < width; ++x) {
                cells[row * width + x] = (IsEdge(x, row) || random.Uniform(0.0f, 1.0f) < wallRate) ? 1 : 0;
            }
        }
        for (int i = 0; i < smoothSteps; ++i) {
            Smooth();
        }
    }

    // 周りの 8 マスに壁が多ければ壁、少なければ床にする
    private void Smooth()
    {
        array<int> next = cells;
        for (int row = 0; row < depth; ++row) {
            for (int x = 0; x < width; ++x) {
                if (IsEdge(x, row)) {
                    continue;
                }
                int walls = CountWallsAround(x, row);
                if (walls > 4) {
                    next[row * width + x] = 1;
                } else if (walls < 4) {
                    next[row * width + x] = 0;
                }
            }
        }
        cells = next;
    }

    // 壁のマスごとにプレハブを置き、まとめるオブジェクトの子にする（全体で 1 回の Undo）
    private void PlaceInScene()
    {
        Editor::BeginUndoGroup("マップを配置");
        GameObject@ old = Editor::FindObject(rootName);
        if (old !is null) {
            Editor::DestroyObject(old);
        }

        GameObject@ root = Editor::CreateObject(rootName);
        float originX = -(width - 1) * cellSize * 0.5f;
        float originZ = -(depth - 1) * cellSize * 0.5f;
        for (int row = 0; row < depth; ++row) {
            for (int x = 0; x < width; ++x) {
                if (cells[row * width + x] == 0) {
                    continue;
                }
                GameObject@ wall = Editor::InstantiatePrefab(wallPrefab, "Wall");
                if (wall is null) {
                    Error("壁のプレハブを読めません: " + wallPrefab);
                    Editor::EndUndoGroup();
                    return;
                }
                wall.transform.position = Vector3(originX + x * cellSize, wallHeight * 0.5f,
                    originZ + (depth - 1 - row) * cellSize);
                wall.transform.scale = Vector3(cellSize, wallHeight, cellSize);
                wall.transform.SetParent(root.transform);
                wall.transform.UpdateMatrix();
            }
        }
        Editor::EndUndoGroup();
        Editor::Select(root);
    }

    private int CountWallsAround(int x, int row)
    {
        int count = 0;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    continue;
                }
                if (!IsInside(x + dx, row + dz) || cells[(row + dz) * width + x + dx] == 1) {
                    ++count;
                }
            }
        }
        return count;
    }

    private int CountWalls()
    {
        int count = 0;
        for (uint i = 0; i < cells.length(); ++i) {
            count += cells[i];
        }
        return count;
    }

    private bool IsInside(int x, int row)
    {
        return x >= 0 && x < width && row >= 0 && row < depth;
    }

    private bool IsEdge(int x, int row)
    {
        return x == 0 || row == 0 || x == width - 1 || row == depth - 1;
    }
}
