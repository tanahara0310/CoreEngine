using namespace CoreEngine;

// スクロールの試験：中身の高さ・スクリプトからの動かし方を最初に確かめ、その後は押された行と選んだ行を Console に出す。
// 選んだ行が窓から出ていたら、窓の外の行が押されたら知らせる。右の Status に位置・速さ・選んでいる行を出す
[DisplayName("試験：UI スクロール")]
class UIScrollProbe : ScriptComponent
{
    private array<GameObject@> rows_;
    private GameObject@ list_;
    private UIText@ status_;
    private int frame_ = 0;
    private int focused_ = -1;
    private int checkFocusFrame_ = -1;

    void Start()
    {
        @list_ = owner.FindObject("List");
        @status_ = owner.FindObject("Status").uiText;
        for (int i = 0; i < 100; ++i) {
            rows_.insertLast(owner.FindObject("Row" + (i < 10 ? "0" : "") + i));
        }
    }

    void Update()
    {
        ++frame_;
        if (frame_ == 5) {
            CheckContent();
        } else if (frame_ == 6) {
            UIScrollView@ scroll = list_.uiScrollView;
            scroll.ScrollTo(rows_[50]);
            Log("UIScrollProbe: ScrollTo(Row50) の後 Row50 は " + (IsVisible(rows_[50]) ? "窓の中" : "窓の外")
                + "・位置 " + scroll.normalizedPosition.y + "（期待 窓の中・0 と 1 の間）");
            scroll.normalizedPosition = Vector2(0.0f, 1.0f);
            Log("UIScrollProbe: 位置を 1 にすると Row99 は " + (IsVisible(rows_[99]) ? "窓の中" : "窓の外")
                + "・Row00 は " + (IsVisible(rows_[0]) ? "窓の中" : "窓の外") + "（期待 窓の中・窓の外）");
            scroll.velocity = Vector2(0.0f, -900.0f);
            scroll.StopMovement();
            Log("UIScrollProbe: 速さを入れて StopMovement すると速さ " + scroll.velocity.y + "（期待 0）");
            scroll.normalizedPosition = Vector2(0.0f, 0.0f);
            rows_[0].uiButton.Focus();
        }

        ReportClicks();
        TrackFocus();
        UpdateStatus();
    }

    void CheckContent()
    {
        Vector4 content = owner.FindObject("Content").rectTransform.rect;
        Vector4 row = rows_[0].rectTransform.rect;
        Log("UIScrollProbe: Content = " + content.z + " x " + content.w + "・行の幅 " + row.z
            + "（期待 600 x 6012・560）");
        Log("UIScrollProbe: 窓の中の行 " + CountVisible() + " 本（期待 12 か 13）");
    }

    void ReportClicks()
    {
        for (uint i = 0; i < rows_.length(); ++i) {
            if (rows_[i].uiButton.wasClicked) {
                Log("UIScrollProbe: " + rows_[i].name + " が押された" + (IsInside(rows_[i]) ? "" : "（窓の外なので押せてはいけない）"));
            }
        }
        GameObject@ outside = owner.FindObject("OutsideButton");
        if (outside.uiButton.wasClicked) {
            Log("UIScrollProbe: 窓の外のボタンが押された");
        }
    }

    void TrackFocus()
    {
        int focused = -1;
        for (uint i = 0; i < rows_.length(); ++i) {
            if (rows_[i].uiButton.focused) {
                focused = int(i);
                break;
            }
        }
        if (focused != focused_) {
            focused_ = focused;
            checkFocusFrame_ = frame_ + 1;
        }
        if (frame_ == checkFocusFrame_ && focused_ >= 0) {
            GameObject@ row = rows_[focused_];
            if (IsVisible(row)) {
                Log("UIScrollProbe: " + row.name + " を選んだ（窓の中に見えている）");
            } else {
                Log("UIScrollProbe: " + row.name + " を選んだが窓から出ている（出てはいけない）");
            }
        }
    }

    void UpdateStatus()
    {
        UIScrollView@ scroll = list_.uiScrollView;
        string focusedName = focused_ >= 0 ? rows_[focused_].name : "なし";
        status_.text = "位置 " + Fixed(scroll.normalizedPosition.y) + "\n速さ " + int(scroll.velocity.y)
            + "\n選んでいる行 " + focusedName + "\n窓の中の行 " + CountVisible() + " 本";
    }

    int CountVisible()
    {
        int count = 0;
        for (uint i = 0; i < rows_.length(); ++i) {
            if (IsInside(rows_[i])) {
                ++count;
            }
        }
        return count;
    }

    // 行の一部でも窓に入っているか
    bool IsInside(GameObject@ row)
    {
        Vector4 view = list_.rectTransform.rect;
        Vector4 r = row.rectTransform.rect;
        return r.y + r.w > view.y && r.y < view.y + view.w;
    }

    // 行がすべて窓に入っているか
    bool IsVisible(GameObject@ row)
    {
        Vector4 view = list_.rectTransform.rect;
        Vector4 r = row.rectTransform.rect;
        return r.y >= view.y - 0.5f && r.y + r.w <= view.y + view.w + 0.5f;
    }

    string Fixed(float value)
    {
        int hundredths = int(floor(value * 100.0f + 0.5f));
        return "" + (hundredths / 100) + "." + (hundredths % 100 < 10 ? "0" : "") + (hundredths % 100);
    }
}
