using namespace CoreEngine;

// タグの試験：プレハブから 1 体出し、Enemy のタグを持つ物を全部集めて持ち上げる
[DisplayName("試験：タグ")]
class TagProbe : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("Enemy のタグを持つプレハブ")]
    string enemyPrefab = "Application/Assets/Prefabs/TaggedEnemy.prefab";

    [Tooltip("集めた物を持ち上げる高さ")]
    float lift = 1.5f;

    private bool checked_ = false;

    void Start()
    {
        GameObject@ spawned = owner.InstantiatePrefab(enemyPrefab, "Enemy_FromPrefab");
        if (spawned is null) {
            Log("TagProbe: プレハブを読めませんでした");
            return;
        }
        spawned.transform.position = Vector3(3.0f, 0.5f, 2.0f);
        Log("TagProbe: プレハブから出した物のタグ = " + spawned.tag + "（期待 Enemy）");
    }

    void Update()
    {
        if (checked_) {
            return;
        }
        checked_ = true;

        array<GameObject@>@ enemies = owner.FindObjectsWithTag("Enemy");
        Log("TagProbe: Enemy のタグを持つ物 " + enemies.length() + " 個（期待 3）");
        for (uint i = 0; i < enemies.length(); ++i) {
            Vector3 position = enemies[i].transform.position;
            position.y += lift;
            enemies[i].transform.position = position;
        }

        GameObject@ plain = owner.FindObject("Plain");
        if (plain !is null) {
            Log("TagProbe: Plain のタグ = " + plain.tag + "・CompareTag(\"Enemy\") = "
                + (plain.CompareTag("Enemy") ? "true" : "false") + "（期待 Untagged・false）");
        }

        GameObject@ player = owner.FindObjectWithTag("Player");
        Log("TagProbe: Player のタグを持つ物 = " + (player is null ? "なし" : player.name) + "（期待 なし）");
    }
}
