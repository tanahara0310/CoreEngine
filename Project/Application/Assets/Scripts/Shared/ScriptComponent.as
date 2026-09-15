// コンポーネントの基底クラス。これを継いだクラスが、クラス名を型名とするコンポーネントになる。
//
// 公開メンバ変数のうち bool / int / float / string がインスペクタに出て、シーンの JSON に保存される。
// private / protected のメンバ変数は出さず、保存もしない。
//
// メンバ変数に付ける属性（[A] [B] と並べても、[A, B] とまとめてもよい）
//   [DisplayName("表示名")]       インスペクタでの名前
//   [Range(最小, 最大)]           編集できる範囲（[Range(最小, 最大, ドラッグ速度)] も書ける）
//   [Speed(ドラッグ速度)]
//   [ReadOnly]                   インスペクタで編集させず、保存もしない
//   [Hidden]                     インスペクタに出さない（保存はする）
//   [Transient]                  保存しない（インスペクタには出す）
//
// クラスに付ける属性
//   [DisplayName("表示名")]       インスペクタのタブとコンポーネント追加の一覧での名前
abstract class ScriptComponent
{
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
}
