// カメラの揺れを手で確かめる。キーを押すとその揺れが 1 回かかる。
//   4 当たり / 5 強い当たり / 6 爆発（球グリッドの中心が爆心） / 7 反動 / 8 着地
//   9 地震（もう一度押すと収まる） / T 押している間ためる / 0 全部止める
//   Tab シーンを読み直す
// 揺れが見えるのはゲーム視点のときだけ（エディタ視点ではゲームのカメラを揺らしても画面は動かない）。
[DisplayName("テスト：カメラの揺れ")]
class CameraShakeTester : ScriptComponent
{
    [Tooltip("爆発の爆心（球グリッドの中心）")]
    Vector3 explosionOrigin = Vector3(0.0f, 9.0f, 0.0f);

    [Range(0.0f, 1.0f)] [Tooltip("T を押している間に 1 フレームでためる量")]
    float traumaPerFrame = 0.06f;

    private uint earthquake_ = 0;

    void Update()
    {
        if (Input::IsKeyTriggered(Key::Tab)) {
            Scene::ChangeScene("TestScene");
            return;
        }

        if (Input::IsKeyTriggered(Key::Num4)) {
            CameraShake::Play(CameraShakePresets::Hit());
            Log("カメラの揺れ: 当たり");
        }
        if (Input::IsKeyTriggered(Key::Num5)) {
            CameraShake::Play(CameraShakePresets::HeavyHit());
            Log("カメラの揺れ: 強い当たり");
        }
        if (Input::IsKeyTriggered(Key::Num6)) {
            // 爆心はカメラの手前側にあるので、押し戻される向きになる
            CameraShake::Play(CameraShakePresets::Explosion(), explosionOrigin);
            CameraShake::Play(CameraShakePresets::Hit());   // 高周波のガタつきを重ねる
            Log("カメラの揺れ: 爆発");
        }
        if (Input::IsKeyTriggered(Key::Num7)) {
            CameraShake::Play(CameraShakePresets::Recoil());
            Log("カメラの揺れ: 反動");
        }
        if (Input::IsKeyTriggered(Key::Num8)) {
            CameraShake::Play(CameraShakePresets::Landing());
            Log("カメラの揺れ: 着地");
        }

        // 止めるまで続く揺れ。もう一度押すと収まる
        if (Input::IsKeyTriggered(Key::Num9)) {
            if (earthquake_ != 0) {
                CameraShake::Stop(earthquake_, 1.0f);
                earthquake_ = 0;
                Log("カメラの揺れ: 地震を止める");
            } else {
                earthquake_ = CameraShake::Play(CameraShakePresets::Earthquake());
                Log("カメラの揺れ: 地震を始める");
            }
        }

        // ためる型。押し続けると強くなり、離すと自然に収まる
        if (Input::IsKeyPressed(Key::T)) {
            CameraShake::AddTrauma(traumaPerFrame);
        }

        if (Input::IsKeyTriggered(Key::Num0)) {
            CameraShake::StopAll(0.3f);
            earthquake_ = 0;
            Log("カメラの揺れ: 全部止める");
        }
    }
}
