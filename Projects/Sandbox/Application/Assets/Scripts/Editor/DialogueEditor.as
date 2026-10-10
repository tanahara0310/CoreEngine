// 会話の流れをノードでつなぐエディタのウィンドウ。Tools > 会話エディタ で開く。
// 何も無いところを右クリックしてノードを足し、ノードの右のピン（出口）から左のピン（入口）へドラッグしてつなぐ。
// 選んだノードとつながりは Delete で消せる。つながりは入口のピンからドラッグして外せる。
// 「保存」でデータファイル（Application/Assets の JSON）へ書き、ゲームのスクリプトから DataFile で読める。
// ノードとつながりは公開メンバ変数なので、スクリプトを読み直しても、エディタを閉じても残る。

// ノードの種類
enum DialogueNodeKind
{
    Start,
    Line,
    Choice,
    End,
}

// ノード 1 つ分のピンの番号の幅（入口 1 つと出口 15 個まで）
const int kDialoguePinStride = 16;

class DialogueNode
{
    int id = 0;
    DialogueNodeKind kind = DialogueNodeKind::Line;
    string speaker = "";
    string text = "";
    array<string> options;
    Vector2 position;
}

class DialogueLink
{
    int id = 0;
    int fromNode = 0;
    int fromSlot = 0;
    int toNode = 0;
}

[MenuItem("Tools/会話エディタ")]
class DialogueEditor : EditorWindow
{
    string dataPath = "Data/Dialogue/Sample.json";
    array<DialogueNode@> nodes;
    array<DialogueLink@> links;
    int nextId = 1;

    private string message_ = "";
    private Vector2 addPosition_;
    private int dropPin_ = -1;
    private int contextNode_ = 0;
    private bool focusStart_ = false;

    void OnEnable()
    {
        if (nodes.length() == 0) {
            CreateSample();
        }
    }

    void OnGUI()
    {
        DrawToolbar();

        EditorGUI::BeginNodeEditor("会話", EditorGUI::NodeEditorFlags::MiniMap);
        for (uint i = 0; i < nodes.length(); ++i) {
            DrawNode(nodes[i]);
        }
        for (uint i = 0; i < links.length(); ++i) {
            DialogueLink@ link = links[i];
            EditorGUI::Link(link.id, OutputPinOf(link.fromNode, link.fromSlot), InputPinOf(link.toNode));
        }
        if (focusStart_) {
            EditorGUI::FocusNode(FindStart());
            focusStart_ = false;
        }
        EditorGUI::EndNodeEditor();

        HandleEditing();
    }

    private void DrawToolbar()
    {
        dataPath = EditorGUI::TextField("保存先", dataPath);
        EditorGUI::Tooltip("Application/Assets からの相対パス（.json）");
        if (EditorGUI::Button("保存")) {
            Save();
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("読み込み")) {
            Load();
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("開始を真ん中へ")) {
            focusStart_ = true;
        }
        EditorGUI::SameLine();
        EditorGUI::TextDisabled(message_ != "" ? message_ : "右クリックで追加、Delete で削除");
    }

    private void DrawNode(DialogueNode@ node)
    {
        EditorGUI::SetNextNodeColor(KindColor(node.kind));
        bool wide = node.kind == DialogueNodeKind::Line || node.kind == DialogueNodeKind::Choice;
        EditorGUI::BeginNode(node.id, node.position, wide ? 220.0f : 110.0f);
        EditorGUI::NodeTitle(KindName(node.kind));
        if (node.kind != DialogueNodeKind::Start) {
            EditorGUI::InputPin(InputPinOf(node.id), "前");
        }

        if (node.kind == DialogueNodeKind::Start) {
            EditorGUI::OutputPin(OutputPinOf(node.id, 0), "始める");
        } else if (node.kind == DialogueNodeKind::Line) {
            node.speaker = EditorGUI::TextField("話す人", node.speaker);
            node.text = EditorGUI::TextArea("##text", node.text, 54.0f);
            EditorGUI::OutputPin(OutputPinOf(node.id, 0), "次");
        } else if (node.kind == DialogueNodeKind::Choice) {
            node.text = EditorGUI::TextField("問い", node.text);
            for (uint i = 0; i < node.options.length(); ++i) {
                EditorGUI::BeginOutputPin(OutputPinOf(node.id, int(i)), EditorGUI::PinShape::TriangleFilled);
                node.options[i] = EditorGUI::TextField("##option" + i, node.options[i]);
                EditorGUI::EndOutputPin();
            }
            EditorGUI::enabled = node.options.length() < uint(kDialoguePinStride - 1);
            if (EditorGUI::Button("+ 選択肢")) {
                node.options.insertLast("");
            }
            EditorGUI::enabled = node.options.length() > 1;
            EditorGUI::SameLine();
            if (EditorGUI::Button("- 最後の選択肢")) {
                RemoveLinksFrom(node.id, int(node.options.length()) - 1);
                node.options.removeLast();
            }
            EditorGUI::enabled = true;
        }

        EditorGUI::EndNode();
        node.position = EditorGUI::GetNodePosition(node.id);
    }

    // つなぐ・外す・足す・消す（EndNodeEditor の後で、その回の出来事を調べる）
    private void HandleEditing()
    {
        int outputPin = 0;
        int inputPin = 0;
        if (EditorGUI::IsLinkCreated(outputPin, inputPin)) {
            Connect(outputPin, inputPin);
        }
        int linkId = 0;
        if (EditorGUI::IsLinkDestroyed(linkId)) {
            RemoveLink(linkId);
        }
        int droppedPin = 0;
        if (EditorGUI::IsLinkDropped(droppedPin) && droppedPin % kDialoguePinStride != 0) {
            // 出口から伸ばして何も無いところで離したら、足したノードへつなぐ
            dropPin_ = droppedPin;
            addPosition_ = EditorGUI::GetNodeEditorMousePosition();
            EditorGUI::OpenPopup("ノードを追加");
        }
        if (EditorGUI::IsNodeEditorHovered() && EditorGUI::IsMouseClicked(MouseButton::Right)) {
            int hoveredNode = 0;
            if (EditorGUI::IsNodeHovered(hoveredNode)) {
                contextNode_ = hoveredNode;
                EditorGUI::OpenPopup("ノードの操作");
            } else {
                dropPin_ = -1;
                addPosition_ = EditorGUI::GetNodeEditorMousePosition();
                EditorGUI::OpenPopup("ノードを追加");
            }
        }
        if (EditorGUI::IsKeyPressed(Key::Delete, false)) {
            DeleteSelection();
        }

        if (EditorGUI::BeginPopup("ノードを追加")) {
            if (EditorGUI::MenuItem("セリフ")) {
                AddNode(DialogueNodeKind::Line);
            }
            if (EditorGUI::MenuItem("選択肢")) {
                AddNode(DialogueNodeKind::Choice);
            }
            if (EditorGUI::MenuItem("終わり")) {
                AddNode(DialogueNodeKind::End);
            }
        }
        EditorGUI::EndPopup();

        if (EditorGUI::BeginPopup("ノードの操作")) {
            DialogueNode@ node = FindNode(contextNode_);
            bool removable = node !is null && node.kind != DialogueNodeKind::Start;
            if (EditorGUI::MenuItem("消す", "Delete", false, removable)) {
                DeleteNode(contextNode_);
            }
        }
        EditorGUI::EndPopup();
    }

    // メニューで選んだノードを右クリックした場所に足し、選んだ状態にする
    private void AddNode(DialogueNodeKind kind)
    {
        DialogueNode@ node = NewNode(kind, addPosition_);
        if (dropPin_ >= 0) {
            Connect(dropPin_, InputPinOf(node.id));
            dropPin_ = -1;
        }
        EditorGUI::ClearNodeSelection();
        EditorGUI::SelectNode(node.id);
    }

    private DialogueNode@ NewNode(DialogueNodeKind kind, const Vector2 &in position)
    {
        DialogueNode@ node = DialogueNode();
        node.id = nextId++;
        node.kind = kind;
        node.position = position;
        if (kind == DialogueNodeKind::Choice) {
            node.options.insertLast("はい");
            node.options.insertLast("いいえ");
        }
        nodes.insertLast(node);
        return node;
    }

    // 出口から入口へつなぐ（1 つの出口からのつながりは 1 本だけにする）
    private void Connect(int outputPin, int inputPin)
    {
        int fromNode = outputPin / kDialoguePinStride;
        int fromSlot = outputPin % kDialoguePinStride - 1;
        int toNode = inputPin / kDialoguePinStride;
        if (fromSlot < 0 || inputPin % kDialoguePinStride != 0 || fromNode == toNode) {
            return;
        }
        for (int i = int(links.length()) - 1; i >= 0; --i) {
            if (links[i].fromNode == fromNode && links[i].fromSlot == fromSlot) {
                links.removeAt(uint(i));
            }
        }
        DialogueLink@ link = DialogueLink();
        link.id = nextId++;
        link.fromNode = fromNode;
        link.fromSlot = fromSlot;
        link.toNode = toNode;
        links.insertLast(link);
    }

    private void DeleteSelection()
    {
        array<int>@ selectedLinks = EditorGUI::GetSelectedLinks();
        for (uint i = 0; i < selectedLinks.length(); ++i) {
            RemoveLink(selectedLinks[i]);
        }
        array<int>@ selectedNodes = EditorGUI::GetSelectedNodes();
        for (uint i = 0; i < selectedNodes.length(); ++i) {
            DeleteNode(selectedNodes[i]);
        }
        EditorGUI::ClearNodeSelection();
    }

    // ノードと、そのノードにつながるつながりを消す（開始のノードは消さない）
    private void DeleteNode(int id)
    {
        for (int i = int(nodes.length()) - 1; i >= 0; --i) {
            if (nodes[i].id == id && nodes[i].kind != DialogueNodeKind::Start) {
                nodes.removeAt(uint(i));
                for (int j = int(links.length()) - 1; j >= 0; --j) {
                    if (links[j].fromNode == id || links[j].toNode == id) {
                        links.removeAt(uint(j));
                    }
                }
                return;
            }
        }
    }

    private void RemoveLink(int id)
    {
        for (int i = int(links.length()) - 1; i >= 0; --i) {
            if (links[i].id == id) {
                links.removeAt(uint(i));
            }
        }
    }

    private void RemoveLinksFrom(int nodeId, int slot)
    {
        for (int i = int(links.length()) - 1; i >= 0; --i) {
            if (links[i].fromNode == nodeId && links[i].fromSlot == slot) {
                links.removeAt(uint(i));
            }
        }
    }

    // ノードを並べた順に、ノードとつながりを配列にしてデータファイルへ書く
    private void Save()
    {
        array<int> ids;
        array<int> kinds;
        array<string> speakers;
        array<string> texts;
        array<float> xs;
        array<float> ys;
        array<int> optionCounts;
        array<string> optionTexts;
        for (uint i = 0; i < nodes.length(); ++i) {
            DialogueNode@ node = nodes[i];
            ids.insertLast(node.id);
            kinds.insertLast(int(node.kind));
            speakers.insertLast(node.speaker);
            texts.insertLast(node.text);
            xs.insertLast(node.position.x);
            ys.insertLast(node.position.y);
            optionCounts.insertLast(int(node.options.length()));
            for (uint j = 0; j < node.options.length(); ++j) {
                optionTexts.insertLast(node.options[j]);
            }
        }
        array<int> linkFrom;
        array<int> linkSlot;
        array<int> linkTo;
        for (uint i = 0; i < links.length(); ++i) {
            linkFrom.insertLast(links[i].fromNode);
            linkSlot.insertLast(links[i].fromSlot);
            linkTo.insertLast(links[i].toNode);
        }

        DataFile@ file = DataFile(dataPath);
        file.SetIntArray("nodeIds", ids);
        file.SetIntArray("nodeKinds", kinds);
        file.SetStringArray("speakers", speakers);
        file.SetStringArray("texts", texts);
        file.SetFloatArray("positionsX", xs);
        file.SetFloatArray("positionsY", ys);
        file.SetIntArray("optionCounts", optionCounts);
        file.SetStringArray("optionTexts", optionTexts);
        file.SetIntArray("linkFrom", linkFrom);
        file.SetIntArray("linkSlot", linkSlot);
        file.SetIntArray("linkTo", linkTo);
        message_ = file.Save() ? "保存しました: " + dataPath : "保存できませんでした: " + dataPath;
    }

    private void Load()
    {
        DataFile@ file = DataFile(dataPath);
        if (!file.exists) {
            message_ = "ファイルがありません: " + dataPath;
            return;
        }
        array<int>@ ids = file.GetIntArray("nodeIds");
        array<int>@ kinds = file.GetIntArray("nodeKinds");
        array<string>@ speakers = file.GetStringArray("speakers");
        array<string>@ texts = file.GetStringArray("texts");
        array<float>@ xs = file.GetFloatArray("positionsX");
        array<float>@ ys = file.GetFloatArray("positionsY");
        array<int>@ optionCounts = file.GetIntArray("optionCounts");
        array<string>@ optionTexts = file.GetStringArray("optionTexts");
        uint count = ids.length();
        if (kinds.length() != count || speakers.length() != count || texts.length() != count
            || xs.length() != count || ys.length() != count || optionCounts.length() != count) {
            message_ = "ノードの数がそろっていません: " + dataPath;
            return;
        }

        nodes.resize(0);
        links.resize(0);
        nextId = 1;
        uint option = 0;
        for (uint i = 0; i < count; ++i) {
            DialogueNode@ node = DialogueNode();
            node.id = ids[i];
            node.kind = DialogueNodeKind(kinds[i]);
            node.speaker = speakers[i];
            node.text = texts[i];
            node.position = Vector2(xs[i], ys[i]);
            for (int j = 0; j < optionCounts[i] && option < optionTexts.length(); ++j) {
                node.options.insertLast(optionTexts[option++]);
            }
            nodes.insertLast(node);
            nextId = Max(nextId, node.id + 1);
        }
        array<int>@ linkFrom = file.GetIntArray("linkFrom");
        array<int>@ linkSlot = file.GetIntArray("linkSlot");
        array<int>@ linkTo = file.GetIntArray("linkTo");
        for (uint i = 0; i < linkFrom.length() && i < linkSlot.length() && i < linkTo.length(); ++i) {
            Connect(OutputPinOf(linkFrom[i], linkSlot[i]), InputPinOf(linkTo[i]));
        }
        message_ = "読み込みました: " + dataPath;
    }

    private void CreateSample()
    {
        nodes.resize(0);
        links.resize(0);
        nextId = 1;
        DialogueNode@ start = NewNode(DialogueNodeKind::Start, Vector2(40.0f, 120.0f));
        DialogueNode@ line = NewNode(DialogueNodeKind::Line, Vector2(200.0f, 80.0f));
        line.speaker = "村人";
        line.text = "ようこそ、旅の人。";
        DialogueNode@ end = NewNode(DialogueNodeKind::End, Vector2(480.0f, 80.0f));
        Connect(OutputPinOf(start.id, 0), InputPinOf(line.id));
        Connect(OutputPinOf(line.id, 0), InputPinOf(end.id));
    }

    private DialogueNode@ FindNode(int id)
    {
        for (uint i = 0; i < nodes.length(); ++i) {
            if (nodes[i].id == id) {
                return nodes[i];
            }
        }
        return null;
    }

    private int FindStart()
    {
        for (uint i = 0; i < nodes.length(); ++i) {
            if (nodes[i].kind == DialogueNodeKind::Start) {
                return nodes[i].id;
            }
        }
        return 0;
    }

    private int InputPinOf(int nodeId)
    {
        return nodeId * kDialoguePinStride;
    }

    private int OutputPinOf(int nodeId, int slot)
    {
        return nodeId * kDialoguePinStride + 1 + slot;
    }

    private string KindName(DialogueNodeKind kind)
    {
        switch (kind) {
        case DialogueNodeKind::Start: return "開始";
        case DialogueNodeKind::Line: return "セリフ";
        case DialogueNodeKind::Choice: return "選択肢";
        }
        return "終わり";
    }

    private Vector4 KindColor(DialogueNodeKind kind)
    {
        switch (kind) {
        case DialogueNodeKind::Start: return Vector4(0.25f, 0.55f, 0.33f, 1.0f);
        case DialogueNodeKind::Line: return Vector4(0.22f, 0.42f, 0.68f, 1.0f);
        case DialogueNodeKind::Choice: return Vector4(0.72f, 0.45f, 0.16f, 1.0f);
        }
        return Vector4(0.35f, 0.35f, 0.4f, 1.0f);
    }
}
