using namespace CoreEngine;

// プレイヤーが触れたら数えて消える
[DisplayName("2D：コイン")]
class Coin : ScriptComponent
{
    [ObjectRef] [Tooltip("数を知らせる先")]
    GameDirector@ director;

    void Start()
    {
        if (director !is null) {
            director.RegisterCoin();
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        if (other.gameObject.name != "Player") {
            return;
        }
        if (director !is null) {
            director.CollectCoin();
        }
        owner.Destroy();
    }
}
