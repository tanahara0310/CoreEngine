using namespace CoreEngine;

// プレイヤーが触れたらクリア
[DisplayName("2D：ゴール")]
class Goal : ScriptComponent
{
    [ObjectRef] [Tooltip("クリアを知らせる先")]
    GameDirector@ director;

    void OnTriggerEnter(Collision@ other)
    {
        if (other.gameObject.name == "Player" && director !is null) {
            director.Clear();
        }
    }
}
