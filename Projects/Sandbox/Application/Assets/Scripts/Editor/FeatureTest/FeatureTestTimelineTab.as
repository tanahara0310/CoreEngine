// 「時間軸」のタブ。キーと区間を並べる時間軸の使い方
//
//   EditorGUI::BeginTimeline(ID, 長さ, 再生位置, 刻み) 〜 EndTimeline()
//       目盛りを描き、目盛りのクリック・ドラッグで動かした再生位置を返す。刻みが 0 より大きいと、キーと再生位置をその間隔にそろえる
//   EditorGUI::TimelineKeys(ラベル, 時刻の配列)      キーのトラック 1 本。ドラッグで動かし、空いたところのダブルクリックで足し、右クリックで消す
//   EditorGUI::TimelineRanges(ラベル, 区間の配列)    区間（x が始まり、y が終わり）のトラック 1 本。端のドラッグで長さ、中のドラッグで位置を変える
//   どちらも配列をその場で書き換え、変えたフレームは true。EndTimeline が再生位置の線を引く
class FeatureTestTimelineTab
{
    FeatureTestChecklist checks;
    float duration = 4.0f;
    float snap = 0.1f;
    float time = 0.0f;
    bool playing = false;
    array<float> cameraKeys = { 0.5f, 2.0f, 3.5f };
    array<float> soundKeys = { 1.0f, 3.0f };
    array<Vector2> lightRanges = { Vector2(0.5f, 1.5f), Vector2(2.5f, 3.8f) };
    string lastPassed = "（まだ）";
    int edits = 0;

    array<string> CheckItems()
    {
        array<string> items = {
            "「再生」で再生位置の線が動き、最後まで行くと最初へ戻る",
            "目盛りをクリック・ドラッグすると再生位置が動く",
            "キーをドラッグで動かすと、刻み（0.1 秒）にそろう",
            "トラックの空いたところをダブルクリックするとキーが足され、キーを右クリックすると消える",
            "区間の端をドラッグすると長さが、中をドラッグすると位置が変わる",
            "再生中にキーを通ると「最後に通ったキー」が変わり、区間の中では「ライト」が点く",
            "刻みを 0 にすると、キーを好きな時刻に置ける",
            "長さを変えると、目盛りの幅が変わる"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        duration = Max(0.5f, EditorGUI::FloatField("長さ（秒）", duration));
        snap = Max(0.0f, EditorGUI::FloatField("刻み（秒）", snap));
        if (EditorGUI::Button(playing ? "止める" : "再生")) {
            playing = !playing;
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("最初へ")) {
            time = 0.0f;
        }

        float previous = time;
        if (playing) {
            time += EditorGUI::GetDeltaTime();
            if (time > duration) {
                time = 0.0f;
                previous = 0.0f;
            }
        }

        time = EditorGUI::BeginTimeline("sequence", duration, time, snap);
        if (EditorGUI::TimelineKeys("カメラ", cameraKeys)) {
            ++edits;
        }
        if (EditorGUI::TimelineKeys("音", soundKeys)) {
            ++edits;
        }
        if (EditorGUI::TimelineRanges("ライト", lightRanges)) {
            ++edits;
        }
        EditorGUI::EndTimeline();

        if (playing) {
            CheckPassed("カメラ", cameraKeys, previous, time);
            CheckPassed("音", soundKeys, previous, time);
        }

        bool lit = false;
        for (uint i = 0; i < lightRanges.length(); ++i) {
            if (time >= lightRanges[i].x && time <= lightRanges[i].y) {
                lit = true;
            }
        }
        EditorGUI::Label("再生位置", formatFloat(time, "", 0, 2) + " 秒");
        EditorGUI::Label("ライト", lit ? "点いている" : "消えている");
        EditorGUI::Label("最後に通ったキー", lastPassed);
        EditorGUI::Label("キーと区間を変えた回数", "" + edits);
        EditorGUI::Label("キーの数", "カメラ " + cameraKeys.length() + "・音 " + soundKeys.length() + "・ライト " + lightRanges.length());
    }

    // 前のフレームの位置から今の位置までの間にあるキーを探す
    private void CheckPassed(const string &in track, const array<float> &in keys, float from, float to)
    {
        for (uint i = 0; i < keys.length(); ++i) {
            if (keys[i] > from && keys[i] <= to) {
                lastPassed = track + " の " + formatFloat(keys[i], "", 0, 2) + " 秒";
            }
        }
    }
}
