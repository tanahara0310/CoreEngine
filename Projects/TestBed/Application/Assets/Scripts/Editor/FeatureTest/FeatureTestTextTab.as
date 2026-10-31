using namespace CoreEngine;

// 「文字」のタブ。文字を出す部品と、区切り・余白・進み具合の棒の使い方
//
//   EditorGUI::Label(文)                 ふつうの文字（ウィンドウの幅で折り返す）
//   EditorGUI::Label(ラベル, 値)         左にラベル、右に値（インスペクタと同じ並び）
//   EditorGUI::TextColored(文, 色)       色付きの文字（色は 0〜1 の RGBA）
//   EditorGUI::TextDisabled(文)          薄い文字（補足に使う）
//   EditorGUI::BulletText(文)            箇条書きの 1 行
//   EditorGUI::Header(文)                見出し付きの区切り線
//   EditorGUI::HelpBox(文, 種類)         説明の囲み（種類は MessageType::Info・Warning・Error）
//   EditorGUI::Tooltip(文)               直前の部品にカーソルを乗せたときに出す説明
//   EditorGUI::Separator()               区切り線
//   EditorGUI::Space(高さ)               縦の余白
//   EditorGUI::ProgressBar(割合, 文)     進み具合の棒（割合は 0〜1、文は棒の上に出す）
class FeatureTestTextTab
{
    FeatureTestChecklist checks;
    bool animate = true;
    float progress = 0.0f;

    array<string> CheckItems()
    {
        array<string> items = {
            "長い文がウィンドウの幅で折り返す（ウィンドウの幅を変えても）",
            "Label(ラベル, 値) のラベルと値が左右に分かれる",
            "色付きの文字がオレンジ、薄い文字が灰色で出る",
            "説明・警告・エラーの囲みが、それぞれ違う色の帯で出る",
            "「ツールチップ付きのボタン」にカーソルを乗せると説明が出る",
            "区切り線と余白が入る",
            "進み具合の棒が動き、% が出る（「動かす」を外すと止まる）"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("文字");
        EditorGUI::Label("ふつうの文字です。長い文は、ウィンドウの幅で折り返します。ウィンドウの端をドラッグして幅を変えると、折り返す場所も変わります。");
        EditorGUI::Label("ラベル", "値");
        EditorGUI::TextColored("色付きの文字", Vector4(0.95f, 0.55f, 0.2f, 1.0f));
        EditorGUI::TextDisabled("薄い文字（補足に使う）");
        EditorGUI::BulletText("箇条書きの 1 つ目");
        EditorGUI::BulletText("箇条書きの 2 つ目");

        EditorGUI::Header("囲み");
        EditorGUI::HelpBox("説明の囲み（MessageType::Info）");
        EditorGUI::HelpBox("警告の囲み（MessageType::Warning）", EditorGUI::MessageType::Warning);
        EditorGUI::HelpBox("エラーの囲み（MessageType::Error）", EditorGUI::MessageType::Error);

        EditorGUI::Header("ツールチップ");
        EditorGUI::Button("ツールチップ付きのボタン");
        EditorGUI::Tooltip("カーソルを乗せると出る説明です");

        EditorGUI::Header("区切りと余白");
        EditorGUI::Label("この下に区切り線");
        EditorGUI::Separator();
        EditorGUI::Label("この下に 24 の余白");
        EditorGUI::Space(24);
        EditorGUI::Label("余白の後の行");

        EditorGUI::Header("進み具合");
        animate = EditorGUI::Toggle("動かす", animate);
        if (animate) {
            progress += EditorGUI::GetDeltaTime() * 0.25f;
            if (progress > 1.0f) {
                progress = 0.0f;
            }
        }
        EditorGUI::ProgressBar(progress, formatInt(int(progress * 100.0f)) + "%");
    }
}
