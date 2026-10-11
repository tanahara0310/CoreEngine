// 「ノード」のタブ。ノードエディタの使い方（数のノードを足し算のノードへつなぎ、表示のノードで結果を見る小さな計算）
//
//   EditorGUI::BeginNodeEditor(ID, NodeEditorFlags, 高さ) 〜 EndNodeEditor()   フラグは MiniMap（全体図）・SnapToGrid（格子に合わせる）
//   EditorGUI::BeginNode(番号, 位置, 幅) 〜 EndNode()      ノード 1 つ。位置は初めて描くときだけ使い、後はドラッグに任せる
//   EditorGUI::GetNodePosition(番号)                      今の位置（毎フレーム読んで覚えておくと、次の起動で同じ位置に出せる）
//   EditorGUI::SetNextNodeColor(色)・SetNextPinColor(色)   次に描くノードの題名・ピンの色
//   EditorGUI::NodeTitle(文)  か  BeginNodeTitle() 〜 EndNodeTitle()（題名に好きな部品を置く）
//   EditorGUI::InputPin・OutputPin(番号, 文, PinShape)     入口（左）と出口（右）のピン
//   EditorGUI::BeginInputPin・BeginOutputPin(番号, PinShape) 〜 End〜   ピンの横に好きな部品を置く
//   EditorGUI::Link(番号, 出口のピン, 入口のピン, 色)        つながり 1 本（EndNodeEditor の前に毎フレーム描く）
// EndNodeEditor の後に呼ぶもの
//   EditorGUI::IsLinkCreated(出口, 入口)   ピンからピンへドラッグして放した（つながりを足すのはスクリプトの仕事）
//   EditorGUI::IsLinkDestroyed(番号)        入口からつながりを引きはがした
//   EditorGUI::IsLinkDropped(ピン)          ピンからドラッグして、何もないところで放した
//   EditorGUI::GetSelectedNodes()・GetSelectedLinks()   選んでいる番号の一覧
//   EditorGUI::SelectNode(番号)・ClearNodeSelection()・FocusNode(番号)   選ぶ・選択を外す・そのノードが見えるように動かす
//   EditorGUI::IsNodeHovered・IsLinkHovered・IsPinHovered(番号)・IsNodeEditorHovered()   カーソルの下
//   EditorGUI::GetNodeEditorMousePosition()  カーソルの位置（ノードの位置と同じ座標）
// ノード・ピン・つながりの番号は、それぞれの中で重ならないようにする（ノード同士で重なると例外）。
// このタブでは、ピンの番号を「ノードの番号 × 10 ＋ 何番目」（1 が出口、2・3 が入口）にしている。
class FeatureTestNodeTab
{
    FeatureTestChecklist checks;
    array<FeatureTestNode@> nodes;
    array<int> linkIds;
    array<int> linkOutputs;
    array<int> linkInputs;
    int nextNodeId = 1;
    int nextLinkId = 1;
    bool miniMap = true;
    bool snap = false;
    string lastEvent = "（まだ）";

    private string hovered_ = "（なし）";
    private int droppedPin_ = 0;
    private Vector2 addPosition_;

    FeatureTestNodeTab()
    {
        Reset();
    }

    array<string> CheckItems()
    {
        array<string> items = {
            "数のノードの値を変えると、足すノードと表示のノードの値が変わる",
            "出口（右のピン）から入口（左のピン）へドラッグしてつなげる。同じ入口へつなぎ直すと前のつながりは外れる",
            "入口のピンからつながりをドラッグすると外れる",
            "ピンからつながりを何もないところへ放すとメニューが出て、足したノードへつながる",
            "何もないところを右クリックすると、カーソルの位置にノードを足せる",
            "ノードやつながりを選んで Delete を押すと消える（ノードを消すとつながりも消える）",
            "ノード・ピン・つながりにカーソルを乗せると「カーソルの下」に番号が出る",
            "「全部選ぶ」「選択を外す」「最初のノードへ」が効く",
            "全体図を入れると隅に全体図が出て、押すとそこへ移る",
            "格子に合わせるを入れると、ノードが格子に吸い付く",
            "ノードの位置とつながりは、ウィンドウを閉じて開き直しても残る"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        miniMap = EditorGUI::Toggle("全体図", miniMap);
        snap = EditorGUI::Toggle("格子に合わせる", snap);
        bool selectAll = EditorGUI::Button("全部選ぶ");
        EditorGUI::SameLine();
        bool clearSelection = EditorGUI::Button("選択を外す");
        EditorGUI::SameLine();
        bool focusFirst = EditorGUI::Button("最初のノードへ");
        EditorGUI::SameLine();
        if (EditorGUI::Button("最初の形に戻す")) {
            Reset();
        }

        int flags = EditorGUI::NodeEditorFlags::None;
        if (miniMap) {
            flags |= EditorGUI::NodeEditorFlags::MiniMap;
        }
        if (snap) {
            flags |= EditorGUI::NodeEditorFlags::SnapToGrid;
        }
        EditorGUI::BeginNodeEditor("graph", flags, 320);
        for (uint i = 0; i < nodes.length(); ++i) {
            DrawNode(nodes[i]);
        }
        for (uint i = 0; i < linkIds.length(); ++i) {
            float value = OutputValue(linkOutputs[i] / 10, 0);
            Vector4 color = value < 0.0f ? Vector4(0.9f, 0.35f, 0.3f, 1.0f) : Vector4(0.4f, 0.75f, 1.0f, 1.0f);
            EditorGUI::Link(linkIds[i], linkOutputs[i], linkInputs[i], color);
        }
        EditorGUI::EndNodeEditor();

        // ここから下は、閉じたばかりのノードエディタへの問い合わせと操作
        if (selectAll) {
            for (uint i = 0; i < nodes.length(); ++i) {
                EditorGUI::SelectNode(nodes[i].id);
            }
        }
        if (clearSelection) {
            EditorGUI::ClearNodeSelection();
        }
        if (focusFirst && nodes.length() > 0) {
            EditorGUI::FocusNode(nodes[0].id);
        }
        HandleEvents();
        DrawAddMenu();

        EditorGUI::Label("カーソルの下", hovered_);
        EditorGUI::Label("選んでいるノード", Join(EditorGUI::GetSelectedNodes()));
        EditorGUI::Label("選んでいるつながり", Join(EditorGUI::GetSelectedLinks()));
        EditorGUI::Label("最後の出来事", lastEvent);
    }

    private void DrawNode(FeatureTestNode@ node)
    {
        EditorGUI::PushID(node.id);
        if (node.kind == 0) {
            EditorGUI::BeginNode(node.id, node.position, 110.0f);
            EditorGUI::NodeTitle("数");
            EditorGUI::BeginOutputPin(node.id * 10 + 1, EditorGUI::PinShape::CircleFilled);
            node.value = EditorGUI::FloatField("##value", node.value);
            EditorGUI::EndOutputPin();
        } else if (node.kind == 1) {
            EditorGUI::SetNextNodeColor(Vector4(0.72f, 0.45f, 0.16f, 1.0f));
            EditorGUI::BeginNode(node.id, node.position, 120.0f);
            EditorGUI::NodeTitle("足す");
            EditorGUI::InputPin(node.id * 10 + 2, "1 つ目", EditorGUI::PinShape::Circle);
            EditorGUI::InputPin(node.id * 10 + 3, "2 つ目", EditorGUI::PinShape::Triangle);
            EditorGUI::OutputPin(node.id * 10 + 1, "= " + formatFloat(OutputValue(node.id, 0), "", 0, 2), EditorGUI::PinShape::QuadFilled);
        } else {
            EditorGUI::SetNextNodeColor(Vector4(0.25f, 0.5f, 0.35f, 1.0f));
            EditorGUI::BeginNode(node.id, node.position, 120.0f);
            // 題名に好きな部品を置く書き方
            EditorGUI::BeginNodeTitle();
            EditorGUI::TextColored("表示", Vector4(0.7f, 1.0f, 0.8f, 1.0f));
            EditorGUI::EndNodeTitle();
            EditorGUI::SetNextPinColor(Vector4(0.95f, 0.6f, 0.2f, 1.0f));
            EditorGUI::InputPin(node.id * 10 + 2, "入力", EditorGUI::PinShape::TriangleFilled);
            EditorGUI::Label("値 " + formatFloat(OutputValue(node.id, 0), "", 0, 2));
        }
        EditorGUI::EndNode();
        node.position = EditorGUI::GetNodePosition(node.id);
        EditorGUI::PopID();
    }

    private void HandleEvents()
    {
        int outputPin = 0;
        int inputPin = 0;
        if (EditorGUI::IsLinkCreated(outputPin, inputPin)) {
            // 出口から入口へ（逆向きにドラッグしても出口・入口の順で届く）
            if (outputPin % 10 == 1 && inputPin % 10 != 1 && outputPin / 10 != inputPin / 10) {
                Connect(outputPin, inputPin);
                lastEvent = "つないだ（" + outputPin + " → " + inputPin + "）";
            }
        }
        int link = 0;
        if (EditorGUI::IsLinkDestroyed(link)) {
            RemoveLink(link);
            lastEvent = "つながり " + link + " を外した";
        }
        int pin = 0;
        if (EditorGUI::IsLinkDropped(pin)) {
            droppedPin_ = pin;
            addPosition_ = EditorGUI::GetNodeEditorMousePosition();
            EditorGUI::OpenPopup("add-node");
            lastEvent = "ピン " + pin + " から何もないところへ放した";
        }

        int id = 0;
        hovered_ = "（なし）";
        if (EditorGUI::IsPinHovered(id)) {
            hovered_ = "ピン " + id;
        } else if (EditorGUI::IsNodeHovered(id)) {
            hovered_ = "ノード " + id;
        } else if (EditorGUI::IsLinkHovered(id)) {
            hovered_ = "つながり " + id;
        } else if (EditorGUI::IsNodeEditorHovered()) {
            Vector2 mouse = EditorGUI::GetNodeEditorMousePosition();
            hovered_ = "何もないところ（" + formatFloat(mouse.x, "", 0, 0) + ", " + formatFloat(mouse.y, "", 0, 0) + "）";
            if (EditorGUI::IsMouseClicked(MouseButton::Right)) {
                droppedPin_ = 0;
                addPosition_ = mouse;
                EditorGUI::OpenPopup("add-node");
            }
        }

        if (EditorGUI::IsWindowFocused() && EditorGUI::IsKeyPressed(Key::Delete, false)) {
            DeleteSelection();
        }
    }

    private void DrawAddMenu()
    {
        if (EditorGUI::BeginPopup("add-node")) {
            EditorGUI::TextDisabled("ノードを足す");
            array<string> names = { "数", "足す", "表示" };
            for (int kind = 0; kind < 3; ++kind) {
                if (EditorGUI::MenuItem(names[kind])) {
                    FeatureTestNode@ added = AddNode(kind, addPosition_);
                    // 出口から放したときは、足したノードの入口へつなぐ
                    if (droppedPin_ % 10 == 1 && kind != 0) {
                        Connect(droppedPin_, added.id * 10 + 2);
                    }
                    lastEvent = names[kind] + " のノード " + added.id + " を足した";
                }
            }
        }
        EditorGUI::EndPopup();
    }

    private void DeleteSelection()
    {
        array<int>@ selectedLinks = EditorGUI::GetSelectedLinks();
        for (uint i = 0; i < selectedLinks.length(); ++i) {
            RemoveLink(selectedLinks[i]);
        }
        array<int>@ selectedNodes = EditorGUI::GetSelectedNodes();
        for (uint i = 0; i < selectedNodes.length(); ++i) {
            for (int j = int(linkIds.length()) - 1; j >= 0; --j) {
                if (linkOutputs[j] / 10 == selectedNodes[i] || linkInputs[j] / 10 == selectedNodes[i]) {
                    RemoveLinkAt(uint(j));
                }
            }
            for (uint j = 0; j < nodes.length(); ++j) {
                if (nodes[j].id == selectedNodes[i]) {
                    nodes.removeAt(j);
                    break;
                }
            }
        }
        if (selectedLinks.length() + selectedNodes.length() > 0) {
            EditorGUI::ClearNodeSelection();
            lastEvent = "ノード " + selectedNodes.length() + " 個とつながり " + selectedLinks.length() + " 本を消した";
        }
    }

    private FeatureTestNode@ AddNode(int kind, const Vector2 &in position)
    {
        FeatureTestNode node;
        node.id = nextNodeId++;
        node.kind = kind;
        node.position = position;
        nodes.insertLast(node);
        return node;
    }

    // 入口には 1 本だけつなぐ（前のつながりは外す）
    private void Connect(int outputPin, int inputPin)
    {
        for (int i = int(linkIds.length()) - 1; i >= 0; --i) {
            if (linkInputs[i] == inputPin) {
                RemoveLinkAt(uint(i));
            }
        }
        linkIds.insertLast(nextLinkId++);
        linkOutputs.insertLast(outputPin);
        linkInputs.insertLast(inputPin);
    }

    private void RemoveLink(int id)
    {
        for (uint i = 0; i < linkIds.length(); ++i) {
            if (linkIds[i] == id) {
                RemoveLinkAt(i);
                return;
            }
        }
    }

    private void RemoveLinkAt(uint index)
    {
        linkIds.removeAt(index);
        linkOutputs.removeAt(index);
        linkInputs.removeAt(index);
    }

    // ノードの出口の値（輪になったつながりで止まらないよう、深さで打ち切る）
    private float OutputValue(int nodeId, int depth)
    {
        FeatureTestNode@ node = Find(nodeId);
        if (node is null || depth > 32) {
            return 0.0f;
        }
        if (node.kind == 0) {
            return node.value;
        }
        if (node.kind == 1) {
            return InputValue(nodeId * 10 + 2, depth + 1) + InputValue(nodeId * 10 + 3, depth + 1);
        }
        return InputValue(nodeId * 10 + 2, depth + 1);
    }

    private float InputValue(int inputPin, int depth)
    {
        for (uint i = 0; i < linkInputs.length(); ++i) {
            if (linkInputs[i] == inputPin) {
                return OutputValue(linkOutputs[i] / 10, depth);
            }
        }
        return 0.0f;
    }

    private FeatureTestNode@ Find(int id)
    {
        for (uint i = 0; i < nodes.length(); ++i) {
            if (nodes[i].id == id) {
                return nodes[i];
            }
        }
        return null;
    }

    private string Join(const array<int>@ values)
    {
        if (values is null || values.length() == 0) {
            return "（なし）";
        }
        string result = "";
        for (uint i = 0; i < values.length(); ++i) {
            result += (i == 0 ? "" : ", ") + values[i];
        }
        return result;
    }

    private void Reset()
    {
        nodes.resize(0);
        linkIds.resize(0);
        linkOutputs.resize(0);
        linkInputs.resize(0);
        nextNodeId = 1;
        nextLinkId = 1;
        FeatureTestNode@ a = AddNode(0, Vector2(10.0f, 10.0f));
        a.value = 2.0f;
        FeatureTestNode@ b = AddNode(0, Vector2(10.0f, 140.0f));
        b.value = 3.0f;
        FeatureTestNode@ sum = AddNode(1, Vector2(140.0f, 60.0f));
        FeatureTestNode@ result = AddNode(2, Vector2(280.0f, 170.0f));
        Connect(a.id * 10 + 1, sum.id * 10 + 2);
        Connect(b.id * 10 + 1, sum.id * 10 + 3);
        Connect(sum.id * 10 + 1, result.id * 10 + 2);
        lastEvent = "最初の形に戻した";
    }
}

// ノードのタブのノード 1 つ（kind は 0 が数・1 が足す・2 が表示）
class FeatureTestNode
{
    int id = 0;
    int kind = 0;
    float value = 0.0f;
    Vector2 position;
}
