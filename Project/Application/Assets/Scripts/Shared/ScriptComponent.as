// コンポーネントの基底クラス。これを継いだクラスが、クラス名を型名とするコンポーネントになる。
// コンポーネントのクラスは 1 ファイルに 1 つ書き、ファイル名をクラス名と同じにする（外れていると、コンパイルのときに警告を出す）。
//
// 公開メンバ変数のうち bool / int / float / string / Vector2 / Vector3 / Vector4 と、それを要素にした array<T> がインスペクタに出て、シーンの JSON に保存される。
// private / protected のメンバ変数は出さず、保存もしない。
//
// メンバ変数に付ける属性（[A] [B] と並べても、[A, B] とまとめてもよい）
//   [DisplayName("表示名")]       インスペクタでの名前
//   [Range(最小, 最大)]           編集できる範囲（[Range(最小, 最大, ドラッグ速度)] も書ける）
//   [Speed(ドラッグ速度)]
//   [ReadOnly]                   インスペクタで編集させず、保存もしない
//   [Hidden]                     インスペクタに出さない（保存はする）
//   [Transient]                  保存しない（インスペクタには出す）
//   [Color]                      Vector4（array<Vector4> なら各要素）を色として編集する
//   [Tooltip("説明")]             インスペクタで項目にカーソルを乗せたときに出す説明
//   [Asset("種類")]               string をアセットの参照にする（種類は Texture / Model / Audio / Prefab など）。
//                                GUID とパスで保存し、メンバ変数にはプロジェクトの根からのパスが入る
//   [ObjectRef]                  GameObject@ か、ScriptComponent を継いだクラスのハンドルを、シーンの別オブジェクトへの参照にする。
//                                インスペクタで繋ぎ先を選び、ID で保存する（クラスのハンドルは、クラス名が同じコンポーネントを指す）
//
// クラスに付ける属性
//   [DisplayName("表示名")]       インスペクタのタブとコンポーネント追加の一覧での名前
//
// エディタのあるビルドは、.as を保存すると動かしたまま読み直す（コンパイルに失敗したときは、直すまで前のスクリプトで動き続ける）。
// 読み直すと、インスペクタに出るメンバ変数の値は持ち越すが、それ以外のメンバ変数は書いた初期値に戻る。
// Tween などエンジンへ渡した関数は呼ばれなくなるので、渡し直しは OnScriptReloaded に書く。
abstract class ScriptComponent
{
    // 持ち主の GameObject（エンジンが入れる）
    private GameObject@ owner_;

    // このコンポーネントを付けている GameObject
    GameObject@ owner { get const { return owner_; } }

    // 付けた直後に 1 回
    void Awake() {}

    // 最初の更新の前に 1 回
    void Start() {}

    // 毎フレーム（GameObject の更新より前）
    void Update() {}

    // 毎フレーム（全オブジェクトの更新の後）
    void LateUpdate() {}

    // 持ち主のオブジェクトを破棄するときに 1 回
    void OnDestroy() {}

    // スクリプトを読み直した後に 1 回（エディタのあるビルドだけ）。
    // 初期値に戻ったメンバ変数の組み直しと、エンジンへ渡す関数の渡し直しをここに書く
    void OnScriptReloaded() {}
}
