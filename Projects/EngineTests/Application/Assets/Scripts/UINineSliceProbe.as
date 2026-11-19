using namespace CoreEngine;

// 9 スライスとゲージの試験：画像の種類と塗る量をスクリプトから読み書きできるかを Console に出し、
// 再生中は右の列のゲージの塗る量を 3 秒で 0 から 1 へ繰り返し動かす
[DisplayName("試験：9 スライスとゲージ")]
class UINineSliceProbe : ScriptComponent
{
    [Tooltip("ゲージが 0 から 1 まで進む秒数")]
    float period = 3.0f;

    private float time_ = 0.0f;
    private bool checked_ = false;

    void Update()
    {
        if (!checked_) {
            checked_ = true;
            Check();
        }
        time_ += Time::DeltaTime();
        float amount = (time_ % period) / period;
        SetAmount("Gauge_H_Live", amount);
        SetAmount("Gauge_V_Live", amount);
        SetAmount("Gauge_R_Live", amount);
    }

    void SetAmount(const string &in name, float amount)
    {
        GameObject@ gauge = owner.FindObject(name);
        if (gauge !is null) {
            gauge.uiImage.fillAmount = amount;
        }
    }

    void Check()
    {
        UIImage@ quarter = owner.FindObject("Gauge_H_25").uiImage;
        Log("UINineSliceProbe: Gauge_H_25 の種類 " + (quarter.imageType == UIImageType::Filled ? "Filled" : "ちがう")
            + "・向き " + (quarter.fillMethod == UIFillMethod::Horizontal ? "Horizontal" : "ちがう")
            + "・塗る量 " + quarter.fillAmount + "（期待 Filled・Horizontal・0.25）");

        UIImage@ radial = owner.FindObject("Gauge_R_50").uiImage;
        Log("UINineSliceProbe: Gauge_R_50 の向き " + (radial.fillMethod == UIFillMethod::Radial360 ? "Radial360" : "ちがう")
            + "・始め " + (radial.fillOrigin == UIFillOrigin::Top ? "Top" : "ちがう")
            + "・時計回り " + (radial.fillClockwise ? "true" : "false") + "（期待 Radial360・Top・true）");

        UIImage@ noCenter = owner.FindObject("Slice_NoCenter").uiImage;
        Log("UINineSliceProbe: Slice_NoCenter の種類 " + (noCenter.imageType == UIImageType::Sliced ? "Sliced" : "ちがう")
            + "・真ん中を描く " + (noCenter.fillCenter ? "true" : "false") + "（期待 Sliced・false）");

        UIImage@ simple = owner.FindObject("Simple_Wide").uiImage;
        simple.imageType = UIImageType::Sliced;
        bool switched = simple.imageType == UIImageType::Sliced;
        simple.imageType = UIImageType::Simple;
        Log("UINineSliceProbe: Simple_Wide を Sliced にして戻す = " + (switched && simple.imageType == UIImageType::Simple ? "できた" : "できない")
            + "（期待 できた）");
    }
}
