// 「マウス」のタブ。部品とウィンドウの状態・マウス・キー・自由に描く場所の使い方
//
//   EditorGUI::IsItemHovered()・IsItemActive()・IsItemClicked(ボタン)   直前の部品の状態
//   EditorGUI::IsWindowHovered()・IsWindowFocused()                    このウィンドウの状態
//   EditorGUI::IsMouseDown・IsMouseClicked・IsMouseDoubleClicked・IsMouseReleased・IsMouseDragging(ボタン)
//   EditorGUI::GetMouseDragDelta(ボタン)    押した位置からのずれ（ドラッグ中）
//   EditorGUI::GetMouseWheel()               このフレームのホイールの量
//   EditorGUI::IsKeyPressed(キー, 繰り返すか)  押したフレーム（繰り返す なら押しっぱなしで何度も）
//   EditorGUI::IsKeyDown(キー)・IsKeyReleased(キー)
//   EditorGUI::ctrl・shift・alt              修飾キーを押しているか
//   EditorGUI::Canvas(幅, 高さ)              自由に描く場所を取る（幅 0 は残り全部）。大きさを返す
//   EditorGUI::DrawRect・DrawRectOutline・DrawLine・DrawText・DrawImage   Canvas の中に描く（左上が 0, 0）
//   EditorGUI::IsCanvasHovered()・GetCanvasMousePosition()               Canvas の上のカーソル
// キーは、このウィンドウを選んでいて、文字の欄に打っていないときだけ届く。
class FeatureTestDeviceTab
{
    FeatureTestChecklist checks;
    string lastClick = "（まだ）";
    int spaceCount = 0;
    int enterCount = 0;
    int releaseCount = 0;
    string lastShortcut = "（まだ）";
    float thickness = 2.0f;
    array<Vector2> lines;
    array<Vector2> marks;
    private bool hovered_ = false;
    private bool active_ = false;
    private Vector2 previous_;

    array<string> CheckItems()
    {
        array<string> items = {
            "ボタンにカーソルを乗せると「乗っている」、押している間は「押している」が はい になる",
            "ボタンを左・右・中のボタンで押すと「最後に押したボタン」が変わる",
            "このウィンドウの上にカーソルがあると「ウィンドウの上」、選ぶと「ウィンドウを選んでいる」が はい",
            "Space は押しっぱなしで増え続け、Enter は押しっぱなしでも 1 回だけ増える",
            "A を押している間だけ「A を押している」が はい、離すと回数が増える",
            "Ctrl+Shift+K で「組み合わせ」が出る。ほかのウィンドウを選ぶとキーが届かない",
            "描く場所を左ドラッグで線が引け、ホイールで太さが変わり、右クリックで全部消える",
            "描く場所をダブルクリックすると、その場所に四角い印が付く",
            "マウスのボタンを押している間・ドラッグ中・ずれが、上の欄に出る"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("部品とウィンドウ");
        EditorGUI::Button("このボタンで試す");
        hovered_ = EditorGUI::IsItemHovered();
        active_ = EditorGUI::IsItemActive();
        if (EditorGUI::IsItemClicked(MouseButton::Left)) {
            lastClick = "左";
        }
        if (EditorGUI::IsItemClicked(MouseButton::Right)) {
            lastClick = "右";
        }
        if (EditorGUI::IsItemClicked(MouseButton::Middle)) {
            lastClick = "中";
        }
        EditorGUI::Label("乗っている", YesNo(hovered_));
        EditorGUI::Label("押している", YesNo(active_));
        EditorGUI::Label("最後に押したボタン", lastClick);
        EditorGUI::Label("ウィンドウの上", YesNo(EditorGUI::IsWindowHovered()));
        EditorGUI::Label("ウィンドウを選んでいる", YesNo(EditorGUI::IsWindowFocused()));

        EditorGUI::Header("マウス");
        EditorGUI::Label("左を押している", YesNo(EditorGUI::IsMouseDown(MouseButton::Left)));
        EditorGUI::Label("右を押している", YesNo(EditorGUI::IsMouseDown(MouseButton::Right)));
        EditorGUI::Label("左でドラッグ中", YesNo(EditorGUI::IsMouseDragging(MouseButton::Left)));
        Vector2 drag = EditorGUI::GetMouseDragDelta(MouseButton::Left);
        EditorGUI::Label("ドラッグのずれ", formatFloat(drag.x, "", 0, 0) + ", " + formatFloat(drag.y, "", 0, 0));

        EditorGUI::Header("キー");
        if (EditorGUI::IsKeyPressed(Key::Space)) {
            ++spaceCount;
        }
        if (EditorGUI::IsKeyPressed(Key::Enter, false)) {
            ++enterCount;
        }
        if (EditorGUI::IsKeyReleased(Key::A)) {
            ++releaseCount;
        }
        if (EditorGUI::ctrl && EditorGUI::shift && EditorGUI::IsKeyPressed(Key::K, false)) {
            lastShortcut = "Ctrl+Shift+K";
        }
        EditorGUI::Label("Space（繰り返す）", "" + spaceCount);
        EditorGUI::Label("Enter（繰り返さない）", "" + enterCount);
        EditorGUI::Label("A を押している", YesNo(EditorGUI::IsKeyDown(Key::A)));
        EditorGUI::Label("A を離した回数", "" + releaseCount);
        EditorGUI::Label("修飾キー", (EditorGUI::ctrl ? "Ctrl " : "") + (EditorGUI::shift ? "Shift " : "") + (EditorGUI::alt ? "Alt" : ""));
        EditorGUI::Label("組み合わせ", lastShortcut);

        EditorGUI::Header("描く場所");
        DrawCanvas();
    }

    private void DrawCanvas()
    {
        Vector2 size = EditorGUI::Canvas(0, 180);
        Vector4 grid = Vector4(1.0f, 1.0f, 1.0f, 0.06f);
        for (float x = 20.0f; x < size.x; x += 20.0f) {
            EditorGUI::DrawLine(Vector2(x, 0.0f), Vector2(x, size.y), grid);
        }
        for (float y = 20.0f; y < size.y; y += 20.0f) {
            EditorGUI::DrawLine(Vector2(0.0f, y), Vector2(size.x, y), grid);
        }

        Vector2 mouse = EditorGUI::GetCanvasMousePosition();
        if (EditorGUI::IsCanvasHovered()) {
            thickness = Clamp(thickness + EditorGUI::GetMouseWheel(), 1.0f, 10.0f);
            if (EditorGUI::IsMouseClicked(MouseButton::Left)) {
                previous_ = mouse;
            }
            if (EditorGUI::IsMouseDown(MouseButton::Left) && lines.length() < 4000) {
                lines.insertLast(previous_);
                lines.insertLast(mouse);
            }
            if (EditorGUI::IsMouseDoubleClicked(MouseButton::Left)) {
                marks.insertLast(mouse);
            }
            if (EditorGUI::IsMouseClicked(MouseButton::Right)) {
                lines.resize(0);
                marks.resize(0);
            }
        }
        previous_ = mouse;

        Vector4 ink = Vector4(0.95f, 0.55f, 0.2f, 1.0f);
        for (uint i = 0; i + 1 < lines.length(); i += 2) {
            EditorGUI::DrawLine(lines[i], lines[i + 1], ink, thickness);
        }
        for (uint i = 0; i < marks.length(); ++i) {
            Vector2 half = Vector2(6.0f, 6.0f);
            EditorGUI::DrawRect(marks[i] - half, marks[i] + half, Vector4(0.3f, 0.7f, 1.0f, 0.5f));
            EditorGUI::DrawRectOutline(marks[i] - half, marks[i] + half, Vector4(0.3f, 0.7f, 1.0f, 1.0f), 1.5f);
        }
        EditorGUI::DrawRectOutline(Vector2(0.0f, 0.0f), size, Vector4(1.0f, 1.0f, 1.0f, 0.25f));
        EditorGUI::DrawText(Vector2(8.0f, 6.0f), "太さ " + formatFloat(thickness, "", 0, 0) + "（ホイールで変える）・線 " + (lines.length() / 2) + " 本",
            Vector4(0.9f, 0.9f, 0.9f, 1.0f));
    }

    private string YesNo(bool value)
    {
        return value ? "はい" : "いいえ";
    }
}
