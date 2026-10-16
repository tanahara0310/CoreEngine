using namespace CoreEngine;

// 「シーンビュー」のタブ。シーンビュー（エディタの Game ウィンドウに映したシーン）に重ねて描き、マウスを受け取る使い方
//
// ウィンドウの OnSceneGUI() は、ウィンドウを開いている間、シーンビューを描くフレームごとに呼ばれる。
// このタブは、開いている間だけ EditorFeatureTest の OnSceneGUI から OnSceneGUI() を呼んでもらう。
//
//   Handles::DrawLine(始点, 終点, 色, 太さ)・DrawPolyline(点の配列, 色, 太さ, 閉じるか)
//   Handles::DrawWireCube(中心, 大きさ, 色)・DrawWireDisc(中心, 向き, 半径, 色)・DrawWireSphere(中心, 半径, 色)
//   Handles::DrawSolidDisc(中心, 向き, 半径, 色)・DrawSolidQuad(4 つの角, 色)・DrawDot(位置, 画素の大きさ, 色)・Label(位置, 文, 色)
//   Handles::PositionHandle(番号, 位置)    矢印のハンドル。ドラッグで動かした位置を返す（番号はハンドルごとに変える）
//   SceneView::isHovered・mousePosition・size・cameraPosition・cameraForward
//   SceneView::GetMouseRay(始点, 向き)          カーソルの下へ伸びるレイ
//   SceneView::RaycastMouse(点, 法線)           カーソルの下の物の表面
//   SceneView::GetObjectUnderMouse()            カーソルの下のオブジェクト
//   SceneView::MouseToPlane(高さ, 点)           カーソルの下の、決めた高さの水平な床の点
//   SceneView::WorldToScreen(位置, 画面の位置)   シーンの位置が画面のどこに映るか
//   SceneView::IsMouseClicked・IsMouseDown・IsMouseReleased・IsMouseDragging(ボタン)・IsKeyPressed(キー)・ctrl・shift・alt
//   SceneView::UseMouse()                      このフレームのクリックでオブジェクトを選ばせない
// Handles:: と SceneView:: は OnSceneGUI の中でだけ使う。描いたものは奥の物に隠れない。
class FeatureTestSceneViewTab
{
    FeatureTestChecklist checks;
    bool drawShapes = true;
    bool useHandle = true;
    bool placeMarks = false;
    Vector3 center = Vector3(0.0f, 0.0f, 0.0f);
    array<Vector3> marks;
    int keyCount = 0;

    private string hoverText_ = "（シーンビューの外）";
    private string rayText_ = "";
    private string hitText_ = "";
    private string floorText_ = "";
    private string cameraText_ = "";
    private string screenText_ = "";
    private string mouseText_ = "";
    private string modifierText_ = "";

    array<string> CheckItems()
    {
        array<string> items = {
            "シーンビューに線・折れ線・箱・円・球・塗った円・塗った四角・点・文字が出る",
            "矢印（位置のハンドル）をドラッグすると図形の中心が動き、下の「中心」の欄も変わる",
            "マウスを動かすと「カーソル」「レイ」「床の点」が変わり、物の上では「当たった点」と名前が出る",
            "カメラを動かすと「カメラ」と「中心の映る位置」が変わる",
            "「クリックで印を置く」を入れると、クリックした場所に印が置かれ、オブジェクトは選ばれない",
            "印を置いている間に Shift・Ctrl・Alt を押すと「修飾キー」に出る",
            "シーンビューの上で K を押すと回数が増える",
            "別のタブへ移ると、シーンビューの図形が消える"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());
        EditorGUI::TextDisabled("シーンビュー（Game ウィンドウ）を見ながら試してください");

        drawShapes = EditorGUI::Toggle("図形を描く", drawShapes);
        useHandle = EditorGUI::Toggle("位置のハンドルを出す", useHandle);
        placeMarks = EditorGUI::Toggle("クリックで印を置く", placeMarks);
        EditorGUI::Tooltip("オンの間は、シーンビューのクリックでオブジェクトを選ばない");
        center = EditorGUI::Vector3Field("中心", center);
        EditorGUI::Label("印の数", "" + marks.length());
        if (EditorGUI::Button("印を消す")) {
            marks.resize(0);
        }

        EditorGUI::Header("シーンビューから読んだ値");
        EditorGUI::Label("カーソル", hoverText_);
        EditorGUI::Label("レイ", rayText_);
        EditorGUI::Label("当たった点", hitText_);
        EditorGUI::Label("床の点", floorText_);
        EditorGUI::Label("カメラ", cameraText_);
        EditorGUI::Label("中心の映る位置", screenText_);
        EditorGUI::Label("マウスのボタン", mouseText_);
        EditorGUI::Label("修飾キー", modifierText_);
        EditorGUI::Label("K を押した回数", "" + keyCount);
    }

    void OnSceneGUI()
    {
        ReadSceneView();
        if (drawShapes) {
            DrawShapes();
        }
        if (useHandle) {
            center = Handles::PositionHandle(1, center);
        }
        if (placeMarks) {
            PlaceMarks();
        }
        for (uint i = 0; i < marks.length(); ++i) {
            Handles::DrawDot(marks[i], 8.0f, Vector4(1.0f, 0.4f, 0.3f, 1.0f));
            Handles::Label(marks[i] + Vector3(0.0f, 0.3f, 0.0f), "印 " + (i + 1), Vector4(1.0f, 0.6f, 0.5f, 1.0f));
        }
    }

    private void ReadSceneView()
    {
        Vector2 mouse = SceneView::mousePosition;
        Vector2 size = SceneView::size;
        hoverText_ = SceneView::isHovered ? Text(mouse) + "（画面 " + Text(size) + "）" : "（シーンビューの外）";

        Vector3 origin;
        Vector3 direction;
        rayText_ = SceneView::GetMouseRay(origin, direction) ? Text(origin) + " から " + Text(direction) + " へ" : "（なし）";

        Vector3 point;
        Vector3 normal;
        if (SceneView::RaycastMouse(point, normal)) {
            GameObject@ object = SceneView::GetObjectUnderMouse();
            hitText_ = Text(point) + (object is null ? "" : "（" + object.name + "）");
            Handles::DrawLine(point, point + normal * 0.5f, Vector4(0.4f, 1.0f, 0.5f, 1.0f));
        } else {
            hitText_ = "（なし）";
        }
        floorText_ = SceneView::MouseToPlane(0.0f, point) ? Text(point) : "（なし）";
        cameraText_ = Text(SceneView::cameraPosition) + " 向き " + Text(SceneView::cameraForward);

        Vector2 screen;
        screenText_ = SceneView::WorldToScreen(center, screen) ? Text(screen) : "（カメラの後ろ）";

        mouseText_ = (SceneView::IsMouseDown(MouseButton::Left) ? "左を押している " : "")
            + (SceneView::IsMouseDragging(MouseButton::Left) ? "ドラッグ中 " : "")
            + (SceneView::IsMouseDown(MouseButton::Right) ? "右を押している" : "");
        modifierText_ = (SceneView::ctrl ? "Ctrl " : "") + (SceneView::shift ? "Shift " : "") + (SceneView::alt ? "Alt" : "");
        if (SceneView::IsKeyPressed(Key::K, false)) {
            ++keyCount;
        }
    }

    private void DrawShapes()
    {
        Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
        Vector4 white = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Vector4 cyan = Vector4(0.4f, 0.9f, 1.0f, 1.0f);
        Vector4 orange = Vector4(1.0f, 0.6f, 0.2f, 1.0f);

        Handles::DrawLine(center, center + Vector3(0.0f, 2.0f, 0.0f), white, 3.0f);
        Handles::DrawDot(center + Vector3(0.0f, 2.0f, 0.0f), 10.0f, orange);
        Handles::Label(center + Vector3(0.0f, 2.4f, 0.0f), "中心", white);

        array<Vector3> star;
        for (int i = 0; i < 10; ++i) {
            float angle = i * 3.14159265f / 5.0f;
            float radius = i % 2 == 0 ? 2.5f : 1.2f;
            star.insertLast(center + Vector3(cos(angle) * radius, 0.05f, sin(angle) * radius));
        }
        Handles::DrawPolyline(star, orange, 2.0f, true);

        Handles::DrawWireDisc(center, up, 3.0f, cyan);
        Handles::DrawWireCube(center + Vector3(4.0f, 0.5f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), cyan);
        Handles::DrawWireSphere(center + Vector3(-4.0f, 0.5f, 0.0f), 0.5f, cyan);
        Handles::DrawSolidDisc(center + Vector3(0.0f, 0.02f, 4.0f), up, 0.8f, Vector4(0.4f, 0.9f, 1.0f, 0.35f));
        Handles::DrawSolidQuad(center + Vector3(-1.0f, 0.02f, -5.0f), center + Vector3(1.0f, 0.02f, -5.0f),
            center + Vector3(1.0f, 0.02f, -3.0f), center + Vector3(-1.0f, 0.02f, -3.0f), Vector4(1.0f, 0.6f, 0.2f, 0.35f));
        Handles::Label(center + Vector3(4.0f, 1.3f, 0.0f), "箱");
        Handles::Label(center + Vector3(-4.0f, 1.3f, 0.0f), "球");
        Handles::Label(center + Vector3(0.0f, 0.3f, 4.0f), "塗った円");
        Handles::Label(center + Vector3(0.0f, 0.3f, -4.0f), "塗った四角");
    }

    private void PlaceMarks()
    {
        // クリックでオブジェクトを選ばせない（ハンドルのドラッグとは別に受け取る）
        SceneView::UseMouse();
        if (!SceneView::IsMouseClicked(MouseButton::Left)) {
            return;
        }
        Vector3 point;
        Vector3 normal;
        if (SceneView::RaycastMouse(point, normal) || SceneView::MouseToPlane(0.0f, point)) {
            marks.insertLast(point);
        }
    }

    private string Text(const Vector2 &in value)
    {
        return formatFloat(value.x, "", 0, 0) + ", " + formatFloat(value.y, "", 0, 0);
    }

    private string Text(const Vector3 &in value)
    {
        return formatFloat(value.x, "", 0, 2) + ", " + formatFloat(value.y, "", 0, 2) + ", " + formatFloat(value.z, "", 0, 2);
    }
}
