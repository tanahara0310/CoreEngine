// 水面テストシーンの見せ画：6 つの構図を黒フェードで巡回する。
// 1 つの構図に固定すると、水面すれすれでは大気の白いもやが画面の半分を占め、
// 高い俯瞰では水の表情が見えない。どれか 1 つを選ぶ必要をなくすための演出。
// 各構図の数値は実機のスクリーンショットで決めてある
//（カメラが埋まる方角・海底メッシュの切れ目が正面に来る方角は候補から外してある）。
[DisplayName("水面：構図の巡回")]
class WaterShowcase : ScriptComponent
{
    [Range(0.5f, 60.0f)] [Tooltip("1 つの構図を見せる時間（秒）")]
    float holdSeconds = 6.0f;

    [Range(0.1f, 10.0f)] [Tooltip("明け／暮れにかける時間（秒）")]
    float fadeSeconds = 1.2f;

    [Range(0.0f, 5.0f)] [Tooltip("暗転したまま保つ時間（秒）")]
    float blackSeconds = 0.4f;

    [Tooltip("巡回する。切ると今の構図のまま止まる")]
    bool cycling = true;

    // 段階: 0 = 明ける / 1 = 見せる / 2 = 暮れる / 3 = 暗転
    private int phase_ = 0;
    private float timer_ = 0.0f;
    private int index_ = 0;
    private bool suspended_ = false;
    private bool wasCycling_ = true;

    private array<Vector3> positions_;
    private array<Vector3> rotations_;
    private array<float> fovs_;

    void Awake()
    {
        // ① 礁湖から外洋へ抜ける水路。手前は浅瀬（コースティクス・砕波泡）、左右を岩と椰子が締める
        AddShot(Vector3(55.0f, 15.0f, -180.0f), Vector3(0.0166f, 0.0942f, 0.0f), 50.0f);
        // ② 南から主島を正面に。島の全景と外洋のうねりが同時に入る
        AddShot(Vector3(60.0f, 14.0f, -160.0f), Vector3(0.0342f, -0.3488f, 0.0f), 55.0f);
        // ③ 北側の浅瀬から順光で。水の透明感とコースティクスが最も出る向き
        AddShot(Vector3(8.0f, 20.0f, 143.0f), Vector3(0.0798f, -3.1416f, 0.0f), 55.0f);
        // ④ 島々に囲まれた内側の礁湖。椰子が両側からフレームになる
        AddShot(Vector3(170.0f, 30.0f, 130.0f), Vector3(0.0708f, -2.5454f, 0.0f), 55.0f);
        // ⑤ 高所からの俯瞰。島の連なりと雲の広がりでスケールを見せる
        AddShot(Vector3(10.0f, 35.0f, 190.0f), Vector3(0.1155f, -3.1216f, 0.0f), 55.0f);
        // ⑥ 外洋から島影を望む。手前は深場のうねりと白波だけの構図
        AddShot(Vector3(-30.0f, 10.0f, -230.0f), Vector3(-0.0187f, 0.1882f, 0.0f), 60.0f);
    }

    void Start()
    {
        // 最初の構図も、巡回中の構図と同じく黒から明ける
        phase_ = 0;
        timer_ = 0.0f;
        index_ = 0;
        wasCycling_ = cycling;
        suspended_ = Scene::IsUsingEditorCamera();
        ApplyShot();
        Rendering::SetFadeAlpha((suspended_ || !cycling) ? 0.0f : 1.0f);
    }

    void Update()
    {
        if (positions_.length() == 0) {
            return;
        }

        // 巡回を切ったら、今見えている構図のまま止める
        if (!cycling) {
            if (wasCycling_) {
                Rendering::SetFadeAlpha(0.0f);
                wasCycling_ = false;
            }
            return;
        }
        if (!wasCycling_) {
            phase_ = 0;
            timer_ = 0.0f;
            wasCycling_ = true;
        }

        // エディタのカメラで覗いている間は、フェードを畳んで止める（段階と経過時間は残す）
        if (Scene::IsUsingEditorCamera()) {
            if (!suspended_) {
                Rendering::SetFadeAlpha(0.0f);
                suspended_ = true;
            }
            return;
        }
        if (suspended_) {
            suspended_ = false;
            Rendering::SetFadeAlpha(PhaseAlpha());
        }

        timer_ += Time::UnscaledDeltaTime();

        const float fade = Max(fadeSeconds, 0.01f);
        const float hold = Max(holdSeconds, 0.01f);

        if (phase_ == 0) {
            const float t = Saturate(timer_ / fade);
            Rendering::SetFadeAlpha(1.0f - t);
            if (t >= 1.0f) { phase_ = 1; timer_ = 0.0f; }
        } else if (phase_ == 1) {
            Rendering::SetFadeAlpha(0.0f);
            if (timer_ >= hold) { phase_ = 2; timer_ = 0.0f; }
        } else if (phase_ == 2) {
            const float t = Saturate(timer_ / fade);
            Rendering::SetFadeAlpha(t);
            if (t >= 1.0f) {
                // 完全に暗転したフレームでだけ構図を差し替える
                index_ = (index_ + 1) % int(positions_.length());
                ApplyShot();
                phase_ = 3;
                timer_ = 0.0f;
            }
        } else {
            // 差し替えの直後は、TAA・自動露出・水面の履歴が追い付くまで黒を保つ
            Rendering::SetFadeAlpha(1.0f);
            if (timer_ >= Max(blackSeconds, 0.0f)) { phase_ = 0; timer_ = 0.0f; }
        }
    }

    void OnDestroy()
    {
        Rendering::SetFadeAlpha(0.0f);
    }

    private void AddShot(const Vector3 &in position, const Vector3 &in rotation, float fovDegrees)
    {
        positions_.insertLast(position);
        rotations_.insertLast(rotation);
        fovs_.insertLast(fovDegrees);
    }

    private void ApplyShot()
    {
        if (index_ < 0 || index_ >= int(positions_.length())) {
            return;
        }
        Transform@ transform = owner.transform;
        if (transform !is null) {
            transform.position = positions_[index_];
            transform.rotation = rotations_[index_];
        }
        Camera@ camera = owner.camera;
        if (camera !is null && camera.exists) {
            camera.fov = fovs_[index_];
        }
    }

    private float PhaseAlpha()
    {
        const float fade = Max(fadeSeconds, 0.01f);
        if (phase_ == 0) { return 1.0f - Saturate(timer_ / fade); }
        if (phase_ == 2) { return Saturate(timer_ / fade); }
        if (phase_ == 3) { return 1.0f; }
        return 0.0f;
    }
}
