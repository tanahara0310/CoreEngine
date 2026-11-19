using namespace CoreEngine;

// UI の親子の試験：名前で子を探す・伸ばした子が親に付いてくる・付け替えても画面の位置が変わらない・
// 自分の子を親にできない、を Console に出す。再生中に押したボタンも出す
[DisplayName("試験：UI の親子")]
class UINestedProbe : ScriptComponent
{
    private bool checked_ = false;

    void Update()
    {
        ReportClick("OkButton", "OK が押された（文字の上でも押せる）");
        ReportClick("HiddenChild", "隠した親の子が押された（押せてはいけない）");
        if (checked_) {
            return;
        }
        checked_ = true;
        Check();
    }

    void ReportClick(const string &in name, const string &in message)
    {
        GameObject@ target = owner.FindObject(name);
        if (target !is null && target.uiButton.wasClicked) {
            Log("UINestedProbe: " + message);
        }
    }

    void Check()
    {
        GameObject@ window = owner.FindObject("Window");
        if (window is null) {
            Log("UINestedProbe: Window が見つかりません");
            return;
        }
        GameObject@ bar = window.FindChild("TitleBar");
        GameObject@ title = window.FindChild("TitleBar/TitleText");
        Log("UINestedProbe: Window の子 " + window.GetChildren().length() + " 個（期待 3）・TitleBar/TitleText = "
            + (title is null ? "なし" : title.name) + "（期待 TitleText）");

        RectTransform@ windowRect = window.rectTransform;
        RectTransform@ barRect = bar.rectTransform;
        Log("UINestedProbe: 幅 Window " + windowRect.rect.z + "・TitleBar " + barRect.rect.z + "（期待 どちらも 600）");

        Vector2 size = windowRect.size;
        windowRect.size = Vector2(800.0f, size.y);
        Log("UINestedProbe: Window の幅を 800 にすると TitleBar " + barRect.rect.z + "（期待 800）");
        windowRect.size = size;

        GameObject@ loose = owner.FindObject("Loose");
        RectTransform@ looseRect = loose.rectTransform;
        Vector4 before = looseRect.rect;
        bool attached = looseRect.SetParent(windowRect);
        Vector4 inParent = looseRect.rect;
        string parentName = looseRect.parent is null ? "なし" : looseRect.parent.gameObject.name;
        @looseRect.parent = null;
        Vector4 after = looseRect.rect;
        Log("UINestedProbe: Loose を Window の子にする = " + (attached ? "できた" : "断られた") + "・親 " + parentName
            + "・画面の四角のずれ " + Difference(before, inParent) + " / 親を外した後 " + Difference(before, after) + "（期待 できた・Window・0 / 0）");

        bool cycle = windowRect.SetParent(barRect);
        Log("UINestedProbe: Window を自分の子 TitleBar の子にする = " + (cycle ? "できた" : "断られた") + "（期待 断られた）");
    }

    float Difference(const Vector4 &in a, const Vector4 &in b)
    {
        return abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z) + abs(a.w - b.w);
    }
}
