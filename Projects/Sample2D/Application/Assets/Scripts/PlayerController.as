using namespace CoreEngine;

// 左右に走り、地面の上でだけ跳ぶ。向きとアニメは動きから決める。穴に落ちたら最初の位置へ戻る
[DisplayName("2D：プレイヤー")]
class PlayerController : ScriptComponent
{
    [Range(0.0f, 20.0f)] [Tooltip("走る速さ（m/s）")]
    float runSpeed = 6.0f;

    [Range(0.0f, 30.0f)] [Tooltip("跳んだ瞬間の上向きの速さ（m/s）")]
    float jumpSpeed = 13.0f;

    [Range(-50.0f, 0.0f)] [Tooltip("これより下へ落ちたら最初の位置へ戻す")]
    float fallLimit = -12.0f;

    private Vector3 start_;

    void Start()
    {
        start_ = owner.transform.position;
    }

    void Update()
    {
        const float move = Input::GetAxis(InputAction::MoveLeft, InputAction::MoveRight);
        const bool grounded = IsGrounded();

        // 横の速さは入力で決め、縦は物理（重力）に任せる
        Rigidbody@ body = owner.rigidbody;
        Vector3 velocity = body.velocity;
        velocity.x = move * runSpeed;
        if (grounded && Input::IsActionTriggered(InputAction::Jump)) {
            velocity.y = jumpSpeed;
        }
        body.velocity = velocity;

        if (move != 0.0f) {
            owner.spriteRenderer.flipX = move < 0.0f;
        }
        if (!grounded) {
            owner.spriteAnimator.Play("Jump");
        } else if (abs(move) > 0.01f) {
            owner.spriteAnimator.Play("Run");
        } else {
            owner.spriteAnimator.Play("Idle");
        }

        if (owner.transform.position.y < fallLimit) {
            owner.transform.position = start_;
            body.velocity = Vector3(0.0f, 0.0f, 0.0f);
        }
    }

    // 足もとへ 3 本のレイを飛ばし、自分以外に当たれば地面の上
    private bool IsGrounded()
    {
        const Vector3 center = owner.transform.position + Vector3(0.0f, -0.4f, 0.0f);
        for (int i = -1; i <= 1; ++i) {
            array<RaycastHit@>@ hits = Physics::RaycastAll(center + Vector3(0.25f * i, 0.0f, 0.0f),
                Vector3(0.0f, -1.0f, 0.0f), 0.15f);
            for (uint k = 0; k < hits.length(); ++k) {
                if (hits[k].gameObject.name != owner.name) {
                    return true;
                }
            }
        }
        return false;
    }
}
