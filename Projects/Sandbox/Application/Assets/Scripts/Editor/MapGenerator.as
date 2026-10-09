// 洞窟のような地形をセル・オートマトンで作り、壁のブロックとしてシーンへ並べるエディタのウィンドウ。
// Tools > マップ生成 で開く。プレビューは左ドラッグで壁、右ドラッグで床に塗れる。
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

    // マスごとの壁（1）と床（0）。上の行が奥
    array<int> cells;
    int cellsWidth = 0;
    int cellsDepth = 0;

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
        smoothSteps = EditorGUI::IntSlider("ならす回数", smoothSteps, 0, 8);
        seed = EditorGUI::IntField("シード", seed);
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
        wallPrefab = EditorGUI::TextField("壁のプレハブ", wallPrefab);
        rootName = EditorGUI::TextField("まとめるオブジェクト", rootName);
        if (EditorGUI::Button("シーンに配置", -1)) {
            PlaceInScene();
        }
        GameObject@ placed = Editor::FindObject(rootName);
        EditorGUI::enabled = placed !is null;
        if (EditorGUI::Button("配置したものを消す", -1)) {
            Editor::DestroyObject(placed);
        }
        EditorGUI::enabled = true;
        EditorGUI::HelpBox("配置と消去は Ctrl+Z で戻せます。残すときはシーンを保存（Ctrl+S）してください。");
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
                if (EditorGUI::IsMouseDown(0)) {
                    cells[hoverRow * width + hoverX] = 1;
                } else if (EditorGUI::IsMouseDown(1)) {
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
