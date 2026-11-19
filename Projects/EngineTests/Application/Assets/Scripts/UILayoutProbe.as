using namespace CoreEngine;

// 並べる部品の試験：縦・横・寄せ・逆の順・格子・大きさ合わせで並んだ子の位置と大きさを Console に出す。
// 位置と大きさは親の左上からの px。再生中に V キーで VHidden を出し入れする
[DisplayName("試験：UI を並べる")]
class UILayoutProbe : ScriptComponent
{
    private int frame_ = 0;
    private int checkFrame_ = 5;

    void Update()
    {
        ++frame_;
        if (frame_ == checkFrame_) {
            Check();
        }
        if (Input::IsKeyTriggered(Key::V)) {
            GameObject@ hidden = owner.FindObject("VHidden");
            hidden.active = !hidden.active;
            Log("UILayoutProbe: VHidden を " + (hidden.active ? "出した" : "隠した"));
            checkFrame_ = frame_ + 2;
        }
    }

    void Check()
    {
        bool hiddenShown = owner.FindObject("VHidden").active;
        Report("縦", "VList", "VA", "10, 20, 360, 60");
        Report("縦", "VList", "VB", "10, 88, 360, 80");
        Report("縦", "VList", "VC", "10, 176, 360, 100");
        if (hiddenShown) {
            Report("縦（出した子）", "VList", "VHidden", "10, 284, 360, 30");
        }
        Report("縦（並べない子は動かない）", "VList", "VIgnored", "320, 340, 60, 60");

        Report("横", "HList", "HA", "10, 10, 100, 180");
        Report("横（余りの取り分 1）", "HList", "HB", "120, 10, 360, 180");
        Report("横", "HList", "HC", "490, 10, 100, 180");

        Report("寄せ（右下）", "Align", "AA", "400, 120, 80, 80");
        Report("寄せ（右下）", "Align", "AB", "490, 140, 60, 60");
        Report("寄せ（右下）", "Align", "AC", "560, 160, 40, 40");

        Report("逆の順", "Reverse", "R3", "0, 0, 300, 100");
        Report("逆の順", "Reverse", "R2", "0, 100, 300, 100");
        Report("逆の順", "Reverse", "R1", "0, 200, 300, 100");

        Report("格子（4 列）", "Grid", "G1", "10, 10, 90, 60");
        Report("格子（4 列）", "Grid", "G4", "310, 10, 90, 60");
        Report("格子（4 列）", "Grid", "G6", "110, 80, 90, 60");
        Report("格子（3 列・中央）", "Grid3", "H31", "50, 65, 100, 80");
        Report("格子（3 列・中央）", "Grid3", "H34", "50, 155, 100, 80");
        Report("格子（3 列・中央）", "Grid3", "H35", "160, 155, 100, 80");

        Vector4 fit = owner.FindObject("FitList").rectTransform.rect;
        Log("UILayoutProbe: 高さを子に合わせる FitList = " + fit.z + " x " + fit.w + "（期待 300 x 150）");
        Report("高さを子に合わせる", "FitList", "F3", "10, 100, 280, 40");

        CheckHorizontalFit();

        Vector4 textFit = owner.FindObject("TextFit").rectTransform.rect;
        Log("UILayoutProbe: 折り返した文字 TextFit = " + textFit.z + " x " + textFit.w
            + "（期待 幅 340 のまま・高さ 78 以上＝3 行以上）");
    }

    void CheckHorizontalFit()
    {
        GameObject@ panel = owner.FindObject("HFit");
        Vector4 outer = panel.rectTransform.rect;
        float x = 12.0f;
        float height = 0.0f;
        string gaps = "";
        for (int i = 1; i <= 3; ++i) {
            Vector4 child = owner.FindObject("HFitText" + i).rectTransform.rect;
            float left = child.x - outer.x;
            gaps += (i > 1 ? "・" : "") + (left - x);
            x = left + child.z + 16.0f;
            height = Max(height, child.w);
        }
        float expectedWidth = x - 16.0f + 12.0f;
        Log("UILayoutProbe: 幅を文字に合わせる HFit = " + outer.z + " x " + outer.w + "（期待 " + expectedWidth + " x "
            + (height + 16.0f) + "）・子の左端のずれ " + gaps + "（期待 0・0・0）");
    }

    void Report(const string &in label, const string &in parentName, const string &in childName, const string &in expected)
    {
        GameObject@ parent = owner.FindObject(parentName);
        GameObject@ child = owner.FindObject(childName);
        if (parent is null || child is null) {
            Log("UILayoutProbe: " + parentName + " か " + childName + " が見つかりません");
            return;
        }
        Vector4 p = parent.rectTransform.rect;
        Vector4 c = child.rectTransform.rect;
        string actual = Round(c.x - p.x) + ", " + Round(c.y - p.y) + ", " + Round(c.z) + ", " + Round(c.w);
        Log("UILayoutProbe: " + (actual == expected ? "合う" : "違う") + " " + label + " " + childName + " = " + actual
            + "（期待 " + expected + "）");
    }

    string Round(float value)
    {
        return "" + int(floor(value + 0.5f));
    }
}
