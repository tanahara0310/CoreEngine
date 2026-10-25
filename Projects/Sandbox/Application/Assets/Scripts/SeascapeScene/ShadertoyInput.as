using namespace CoreEngine;

// Input::GetMousePosition の基準解像度
const float kShadertoyReferenceWidth = 1920.0f;
const float kShadertoyReferenceHeight = 1080.0f;

// Shadertoy から移したポストエフェクトを有効にし、iTime と iMouse の元になる値を毎フレーム渡す
// iTime は再生を始めてからの秒数、iMouse はゲーム画面の上で左ボタンをドラッグした位置（離しても残る）
[DisplayName("Shadertoy の入力")]
class ShadertoyInput : ScriptComponent
{
    [Tooltip("値を渡すポストエフェクト（Project.json の postEffects の名前）")]
    string effectName = "Seascape";

    private float time_ = 0.0f;
    private Vector2 mouse_ = Vector2(0.0f, 0.0f);
    private bool dragging_ = false;

    void Start()
    {
        PostEffect::SetEnabled(effectName, true);
        Send();
    }

    void Update()
    {
        time_ += Time::DeltaTime();

        // ゲーム画面の上で押したときだけドラッグを始め、画面の上にある間だけ位置を取る
        if (Input::IsMouseButtonTriggered(MouseButton::Left) && Input::IsPointerOverGame()) {
            dragging_ = true;
        }
        if (!Input::IsMouseButtonPressed(MouseButton::Left)) {
            dragging_ = false;
        }
        if (dragging_ && Input::IsPointerOverGame()) {
            const Vector2 position = Input::GetMousePosition();
            mouse_ = Vector2(position.x / kShadertoyReferenceWidth, 1.0f - position.y / kShadertoyReferenceHeight);
        }

        Send();
    }

    // 秒数とマウスの位置（左下が (0, 0) の割合）をマテリアルへ入れる
    private void Send()
    {
        ShaderMaterial@ material = PostEffect::GetMaterial(effectName);
        if (material is null) {
            return;
        }
        material.SetFloat("iTime", time_);
        material.SetVector("mouse", Vector4(mouse_.x, mouse_.y, 0.0f, 0.0f));
    }
}
