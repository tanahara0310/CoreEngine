// 「グラフとカーブ」のタブ。数の並びのグラフと、点をドラッグして作るカーブの使い方
//
//   EditorGUI::PlotLines(ラベル, 値の配列, 高さ)               折れ線（縦の範囲は値に合わせる）
//   EditorGUI::PlotLines(ラベル, 値の配列, 最小, 最大, 高さ)   縦の範囲を決めた折れ線
//   EditorGUI::PlotHistogram(…)                                棒のグラフ（引数は PlotLines と同じ）
//   EditorGUI::GetDeltaTime()                                  前のフレームからの秒数（エディタの中で動きを付けるのに使う）
//   EditorGUI::CurveField(ラベル, 点の配列, 範囲の最小, 範囲の最大, 高さ)   点（Vector2）をドラッグで動かし、
//                     ダブルクリックで足し、右クリックで消すカーブの欄。点の配列をその場で書き換え、変えたフレームは true
//   Curve::Evaluate(点の配列, x)                                点をなめらかにつないだ曲線の x での値（ゲームのスクリプトでも使える）
class FeatureTestGraphTab
{
    FeatureTestChecklist checks;
    array<float> counts = { 3.0f, 7.0f, 2.0f, 9.0f, 5.0f, 4.0f, 8.0f };
    array<Vector2> curve = { Vector2(0.0f, 0.0f), Vector2(0.3f, 0.8f), Vector2(1.0f, 1.0f) };
    float probe = 0.5f;
    array<Vector2> wideCurve = { Vector2(0.0f, -2.0f), Vector2(4.0f, 4.0f), Vector2(10.0f, 0.0f) };
    int curveEdits = 0;
    private float phase_ = 0.0f;
    private array<float> history_;

    array<string> CheckItems()
    {
        array<string> items = {
            "波の折れ線が動き続ける（GetDeltaTime で進めている）",
            "範囲を決めない折れ線（フレームの秒数）は、値に合わせて縦の幅が変わる",
            "棒のグラフが出て、「ばらつかせる」で棒の高さが変わる",
            "カーブの点をドラッグで動かせ、ダブルクリックで足し、点を右クリックで消せる",
            "カーブを変えると「カーブを変えた回数」が増え、下の Evaluate の折れ線も同じ形に変わる",
            "x のスライダーを動かすと Curve::Evaluate の値が変わる",
            "範囲を変えたカーブ（横 0〜10・縦 -5〜5）の目盛りがその範囲になる"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("グラフ");
        float delta = EditorGUI::GetDeltaTime();
        phase_ += delta * 2.0f;
        array<float> wave;
        for (int i = 0; i < 64; ++i) {
            wave.insertLast(sin(i * 0.2f + phase_));
        }
        EditorGUI::PlotLines("波（-1〜1）", wave, -1.0f, 1.0f, 60);

        history_.insertLast(delta * 1000.0f);
        if (history_.length() > 120) {
            history_.removeAt(0);
        }
        EditorGUI::PlotLines("フレームの ms", history_, 50);

        EditorGUI::PlotHistogram("回数", counts, 0.0f, 10.0f, 60);
        if (EditorGUI::Button("ばらつかせる")) {
            for (uint i = 0; i < counts.length(); ++i) {
                counts[i] = fmod(counts[i] * 3.7f + 1.3f, 10.0f);
            }
        }

        EditorGUI::Header("カーブ");
        if (EditorGUI::CurveField("カーブ", curve)) {
            ++curveEdits;
        }
        EditorGUI::Label("カーブを変えた回数", "" + curveEdits);
        array<float> samples;
        for (int i = 0; i <= 64; ++i) {
            samples.insertLast(Curve::Evaluate(curve, i / 64.0f));
        }
        EditorGUI::PlotLines("Evaluate で読んだ値", samples, 0.0f, 1.0f, 60);
        probe = EditorGUI::Slider("x", probe, 0.0f, 1.0f);
        EditorGUI::Label("Curve::Evaluate(x)", formatFloat(Curve::Evaluate(curve, probe), "", 0, 3));

        EditorGUI::Header("範囲を変えたカーブ");
        EditorGUI::CurveField("横 0〜10・縦 -5〜5", wideCurve, Vector2(0.0f, -5.0f), Vector2(10.0f, 5.0f), 140);
        if (EditorGUI::Button("カーブを最初の形に戻す")) {
            array<Vector2> initialCurve = { Vector2(0.0f, 0.0f), Vector2(0.3f, 0.8f), Vector2(1.0f, 1.0f) };
            array<Vector2> initialWide = { Vector2(0.0f, -2.0f), Vector2(4.0f, 4.0f), Vector2(10.0f, 0.0f) };
            curve = initialCurve;
            wideCurve = initialWide;
        }
    }
}
