// 「並べ方」のタブ。部品の並べ方・まとめ方・押せなくする方法
//
//   EditorGUI::SameLine()                次の部品を同じ行の右に置く
//   EditorGUI::GetAvailableWidth()       今の行で使える残りの幅（ボタンの幅を割り振るのに使う）
//   EditorGUI::BeginGroup() 〜 EndGroup()          中の部品を 1 つの塊にする（塊の後ろに SameLine で列を並べられる）
//   EditorGUI::BeginChild(ID, 幅, 高さ, 枠線) 〜 EndChild()  中だけスクロールする枠（幅・高さ 0 は残り全部）
//   EditorGUI::indentLevel               字下げの段（0 から。枠・表・ポップアップの中では 0 から数え直す）
//   EditorGUI::enabled                   false の間に描いた部品は薄くなり、押せない（描き終えたら true に戻す）
//   EditorGUI::PushID(番号か文字) 〜 PopID()  同じラベルの部品を並べるときに区別する
//   EditorGUI::Foldout(文, 最初は開くか)  押すと開閉する見出し（開いていれば true）
// Begin〜 は、戻り値が false でも End〜 と組にする。
class FeatureTestLayoutTab
{
    FeatureTestChecklist checks;
    bool locked = false;
    float lockedValue = 0.5f;
    int lastPressed = -1;
    string lastNamed = "（まだ）";
    private bool groupHovered_ = false;

    array<string> CheckItems()
    {
        array<string> items = {
            "A・B・C のボタンが横に並ぶ",
            "左半分・右半分のボタンが幅を分け合う（ウィンドウの幅を変えても）",
            "左のグループにカーソルを乗せると「グループの上」が はい になる。右の列はグループの横に並ぶ",
            "枠の中だけがスクロールし、40 行目まで見える。枠線の無い枠もある",
            "字下げ 1・2 が段々に下がる",
            "「押せなくする」を入れると、ボタンとスライダーが薄くなり、押しても動かない",
            "同じ「押す」ボタン 3 つが別々に反応し、押した番号が出る",
            "折りたたみを押すと中身が出たり隠れたりする"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("横に並べる");
        EditorGUI::Button("A");
        EditorGUI::SameLine();
        EditorGUI::Button("B");
        EditorGUI::SameLine();
        EditorGUI::Button("C");
        // 2 つのボタンの間の隙間（8）を引いてから半分にする
        float half = Max(1.0f, (EditorGUI::GetAvailableWidth() - 8.0f) * 0.5f);
        EditorGUI::Button("左半分", half);
        EditorGUI::SameLine();
        EditorGUI::Button("右半分", half);
        EditorGUI::Label("使える幅", formatFloat(EditorGUI::GetAvailableWidth(), "", 0, 0));

        EditorGUI::Header("グループ");
        EditorGUI::BeginGroup();
        EditorGUI::Label("グループの 1 行目");
        EditorGUI::Label("グループの 2 行目");
        EditorGUI::Button("グループの中のボタン");
        EditorGUI::EndGroup();
        // グループは 1 つの部品として扱われるので、直前の部品の状態で調べられる
        groupHovered_ = EditorGUI::IsItemHovered();
        EditorGUI::SameLine();
        EditorGUI::BeginGroup();
        EditorGUI::TextDisabled("右の列 1");
        EditorGUI::TextDisabled("右の列 2");
        EditorGUI::EndGroup();
        EditorGUI::Label("グループの上", groupHovered_ ? "はい" : "いいえ");

        EditorGUI::Header("枠（子ウィンドウ）");
        if (EditorGUI::BeginChild("rows", 0, 100)) {
            for (int i = 1; i <= 40; ++i) {
                EditorGUI::Label("行 " + i);
            }
        }
        EditorGUI::EndChild();
        if (EditorGUI::BeginChild("plain", 0, 40, false)) {
            EditorGUI::TextDisabled("枠線の無い枠");
        }
        EditorGUI::EndChild();

        EditorGUI::Header("字下げ");
        EditorGUI::Label("字下げ 0");
        EditorGUI::indentLevel = 1;
        EditorGUI::Label("字下げ 1");
        EditorGUI::indentLevel = 2;
        EditorGUI::Label("字下げ 2");
        EditorGUI::indentLevel = 0;

        EditorGUI::Header("押せなくする");
        locked = EditorGUI::Toggle("押せなくする", locked);
        EditorGUI::enabled = !locked;
        if (EditorGUI::Button("押せるときだけ押せるボタン")) {
            lastNamed = "押せるときだけ押せるボタン";
        }
        lockedValue = EditorGUI::Slider("押せるときだけ動く", lockedValue, 0.0f, 1.0f);
        EditorGUI::enabled = true;

        EditorGUI::Header("同じラベルを並べる（PushID）");
        for (int i = 0; i < 3; ++i) {
            EditorGUI::PushID(i);
            if (EditorGUI::Button("押す")) {
                lastPressed = i;
            }
            EditorGUI::PopID();
            EditorGUI::SameLine();
        }
        EditorGUI::PushID("named");
        if (EditorGUI::Button("押す")) {
            lastNamed = "文字の ID の「押す」";
        }
        EditorGUI::PopID();
        EditorGUI::Label("押した番号", lastPressed < 0 ? "（まだ）" : "" + lastPressed);
        EditorGUI::Label("最後に押したもの", lastNamed);

        EditorGUI::Header("折りたたみ");
        if (EditorGUI::Foldout("押すと開く", false)) {
            EditorGUI::indentLevel = 1;
            EditorGUI::Label("折りたたみの中身");
            EditorGUI::indentLevel = 0;
        }
    }
}
