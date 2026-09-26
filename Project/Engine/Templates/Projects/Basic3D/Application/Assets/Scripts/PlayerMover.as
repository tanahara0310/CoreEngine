// 移動の操作（WASD・矢印キー・左スティック）で、持ち主を地面に沿って動かす
[DisplayName("プレイヤーの移動")]
class PlayerMover : ScriptComponent
{
    [Range(0, 30)]
    [DisplayName("速さ")]
    [Tooltip("1 秒に進む距離（m）")]
    float speed = 5.0f;

    // 毎フレーム
    void Update()
    {
        const float dt = Time::DeltaTime();
        const Vector2 move = Input::GetAxis2D(InputAction::MoveLeft, InputAction::MoveRight,
                                              InputAction::MoveBack, InputAction::MoveForward);
        owner.transform.position = owner.transform.position + Vector3(move.x, 0.0f, move.y) * (speed * dt);
    }
}
