// プロジェクトの Scripts/Editor にあるこのファイルは、エディタが起動のたびに原本（Engine/Templates/Scripts/Editor/EditorWindow.as）から書き直す。
// 中身を変えるときは原本を直す。
//
// エディタのウィンドウの基底クラス。これを継いだクラスが、エディタのメニューから開けるウィンドウになる。
// Scripts/Editor フォルダの中のスクリプトはエディタだけで読み込み、書き出したゲームには入らない。
// EditorWindow・EditorGUI・Editor:: は、このフォルダの中だけで使う（外で使うと、書き出したゲームでコンパイルできない）。
//
// クラスに付ける属性
//   [MenuItem("Tools/マップ生成")]   ウィンドウを開くメニューの場所。最後の区切りがウィンドウの題名になる
//                                   （付けなければ Tools/クラス名）
//
// 関数に付ける属性（クラスの外に書いた、引数も戻り値も無い関数）
//   [MenuItem("Tools/選んだ物を整列")]  メニューから選ぶと、その関数を 1 回呼ぶ
//
// 画面の部品は EditorGUI::、編集中のシーンの操作は Editor:: にある。
// Editor:: で作ったオブジェクトと消したオブジェクトは、Ctrl+Z で戻せる。
//
// .as を保存すると、開いたまま読み直す。メンバ変数の値（数値・bool・string・Vector・それらの array）は持ち越す。
abstract class EditorWindow
{
    // 開いたとき（読み直した後に開いていたときも）
    void OnEnable() {}

    // 開いている間、毎フレーム。ここで EditorGUI:: を呼んで中身を描く
    void OnGUI() {}

    // 閉じたとき（読み直す前に開いていたときも）
    void OnDisable() {}
}
