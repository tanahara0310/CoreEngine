using namespace CoreEngine;

// マスクの試験：押せたボタンを Console に出す。再生中に M キーで MaskA の切り取りを入り切りする
[DisplayName("試験：UI マスク")]
class UIMaskProbe : ScriptComponent
{
    private bool checked_ = false;

    void Update()
    {
        if (!checked_) {
            checked_ = true;
            GameObject@ maskA = owner.FindObject("MaskA");
            Log("UIMaskProbe: MaskA のマスク = " + (maskA.uiMask.enabled ? "入" : "切") + "・MaskB の親 = "
                + owner.FindObject("MaskB").rectTransform.parent.gameObject.name + "（期待 入・MaskA）");
        }
        ReportClick("EdgeButton", "EdgeButton が押された（マスクの中の左半分）");
        ReportClick("GhostButton", "GhostButton が押された（マスクの外なので押せてはいけない）");

        if (Input::IsKeyTriggered(Key::M)) {
            UIMask@ mask = owner.FindObject("MaskA").uiMask;
            mask.enabled = !mask.enabled;
            Log("UIMaskProbe: MaskA のマスクを " + (mask.enabled ? "入" : "切") + "にした");
        }
    }

    void ReportClick(const string &in name, const string &in message)
    {
        GameObject@ target = owner.FindObject(name);
        if (target !is null && target.uiButton.wasClicked) {
            Log("UIMaskProbe: " + message);
        }
    }
}
