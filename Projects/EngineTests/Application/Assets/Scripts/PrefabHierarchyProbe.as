using namespace CoreEngine;

// 親子つきのプレハブの試験：シーンに置いた組と、スクリプトで出した組の、親子・位置・参照・破棄を確かめる
[DisplayName("試験：親子つきのプレハブ")]
class PrefabHierarchyProbe : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("根（Robot）・子（Body）・孫（Hat）を持つプレハブ")]
    string robotPrefab = "Application/Assets/Prefabs/Robot.prefab";

    [Tooltip("スクリプトで出す組の数")]
    int spawnCount = 100;

    [Tooltip("プレハブでの、根から孫（Hat）までの高さ")]
    float hatHeight = 1.7f;

    [Tooltip("プレハブでの、孫（Hat）の大きさ。Robot_B はこの 2 倍に上書きしてある")]
    float hatScale = 0.6f;

    private int frame_ = 0;
    private int failures_ = 0;
    private array<GameObject@> spawned_;
    private array<GameObject@> spawnedChildren_;

    void Update()
    {
        ++frame_;
        if (frame_ == 2) {
            CheckPlaced();
            Spawn();
        } else if (frame_ == 4) {
            CheckSpawned();
            DestroySpawned();
        } else if (frame_ == 6) {
            CheckDestroyed();
            if (failures_ == 0) {
                Log("PrefabHierarchyProbe: 全部合う");
            } else {
                Warn("PrefabHierarchyProbe: 違う項目が " + failures_ + " 個");
            }
        }
    }

    // シーンに置いて保存してある 2 組を確かめる
    private void CheckPlaced()
    {
        GameObject@ a = owner.FindObject("Robot_A");
        GameObject@ b = owner.FindObject("Robot_B");
        Result(a !is null && b !is null, "置いた組", "Robot_A と Robot_B がシーンにある");
        if (a is null || b is null) {
            return;
        }
        CheckRobot(a, "Robot_A");
        CheckRobot(b, "Robot_B");

        // Robot_B は孫の大きさを上書きし、子を 1 つ足してある
        GameObject@ hatA = a.FindChild("Body/Hat");
        GameObject@ hatB = b.FindChild("Body/Hat");
        if (hatA !is null && hatB !is null) {
            const float scaleA = hatA.transform.scale.x;
            const float scaleB = hatB.transform.scale.x;
            Result(Near(scaleA, hatScale), "上書きなし", "Robot_A の Hat の大きさ " + F(scaleA) + "（期待 " + F(hatScale) + "）");
            Result(Near(scaleB, hatScale * 2.0f), "上書き", "Robot_B の Hat の大きさ " + F(scaleB) + "（期待 " + F(hatScale * 2.0f) + "）");
        }
        Result(a.GetChildren().length() == 1, "子の数", "Robot_A の子 " + a.GetChildren().length() + " 個（期待 1）");
        Result(b.GetChildren().length() == 2, "足した子", "Robot_B の子 " + b.GetChildren().length() + " 個（期待 2）");
    }

    // 根 1 つの、子・孫・位置・参照を確かめる
    private void CheckRobot(GameObject@ root, const string &in label)
    {
        GameObject@ body = root.FindChild("Body");
        GameObject@ hat = root.FindChild("Body/Hat");
        Result(body !is null && hat !is null, label + " の親子", "子 Body と孫 Hat がある");
        if (body is null || hat is null) {
            return;
        }

        const Vector3 rootPosition = root.transform.worldPosition;
        const Vector3 hatPosition = hat.transform.worldPosition;
        const float dx = hatPosition.x - rootPosition.x;
        const float dy = hatPosition.y - rootPosition.y;
        const float dz = hatPosition.z - rootPosition.z;
        Result(Near(dx, 0.0f) && Near(dy, hatHeight) && Near(dz, 0.0f), label + " の位置",
               "根から Hat まで (" + F(dx) + ", " + F(dy) + ", " + F(dz) + ")（期待 0.000, " + F(hatHeight) + ", 0.000）");

        PrefabRobotBrain@ brain;
        const bool pointsOwnBody = root.GetComponent(@brain) && brain.body !is null && brain.body == body;
        Result(pointsOwnBody, label + " の参照", "根のスクリプトの欄が自分の Body を指す");
    }

    private void Spawn()
    {
        for (int i = 0; i < spawnCount; ++i) {
            GameObject@ robot = owner.InstantiatePrefab(robotPrefab, "Spawned_" + i);
            if (robot is null) {
                Result(false, "出す", "プレハブ " + robotPrefab + " を読めない");
                return;
            }
            robot.transform.position = Vector3(float(i % 10) * 2.0f - 9.0f, 0.0f, 6.0f + float(i / 10) * 2.0f);
            spawned_.insertLast(robot);
        }
    }

    private void CheckSpawned()
    {
        Result(int(spawned_.length()) == spawnCount, "出した数", spawned_.length() + " 組（期待 " + spawnCount + "）");
        if (spawned_.length() < 2) {
            return;
        }
        GameObject@ first = spawned_[0];
        GameObject@ last = spawned_[spawned_.length() - 1];
        CheckRobot(first, "出した最初の組");
        CheckRobot(last, "出した最後の組");

        PrefabRobotBrain@ firstBrain;
        PrefabRobotBrain@ lastBrain;
        if (first.GetComponent(@firstBrain) && last.GetComponent(@lastBrain) &&
            firstBrain.body !is null && lastBrain.body !is null) {
            Result(!(firstBrain.body == lastBrain.body), "組ごとの参照", "最初の組と最後の組は別々の Body を指す");
        }

        for (uint i = 0; i < spawned_.length(); ++i) {
            GameObject@ body = spawned_[i].FindChild("Body");
            GameObject@ hat = spawned_[i].FindChild("Body/Hat");
            if (body !is null) {
                spawnedChildren_.insertLast(body);
            }
            if (hat !is null) {
                spawnedChildren_.insertLast(hat);
            }
        }
        Result(int(spawnedChildren_.length()) == spawnCount * 2, "出した子と孫",
               spawnedChildren_.length() + " 個（期待 " + (spawnCount * 2) + "）");
    }

    private void DestroySpawned()
    {
        for (uint i = 0; i < spawned_.length(); ++i) {
            spawned_[i].Destroy();
        }
    }

    private void CheckDestroyed()
    {
        int aliveRoots = 0;
        for (uint i = 0; i < spawned_.length(); ++i) {
            if (spawned_[i].isAlive) {
                ++aliveRoots;
            }
        }
        int aliveChildren = 0;
        for (uint i = 0; i < spawnedChildren_.length(); ++i) {
            if (spawnedChildren_[i].isAlive) {
                ++aliveChildren;
            }
        }
        Result(aliveRoots == 0, "根の破棄", "残った根 " + aliveRoots + " 個（期待 0）");
        Result(aliveChildren == 0, "子の破棄", "根を消した後に残った子と孫 " + aliveChildren + " 個（期待 0）");
    }

    private void Result(bool ok, const string &in name, const string &in text)
    {
        if (ok) {
            Log("PrefabHierarchyProbe: OK " + name + ": " + text);
        } else {
            ++failures_;
            Warn("PrefabHierarchyProbe: NG " + name + ": " + text);
        }
    }

    private bool Near(float value, float expected)
    {
        const float difference = value - expected;
        return difference < 0.001f && difference > -0.001f;
    }

    private string F(float value)
    {
        return formatFloat(value, "", 0, 3);
    }
}
