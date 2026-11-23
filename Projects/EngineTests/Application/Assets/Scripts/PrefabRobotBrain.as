using namespace CoreEngine;

// 親子つきのプレハブの試験：プレハブの根に付け、プレハブの中の子を指す
[DisplayName("試験：プレハブの根")]
class PrefabRobotBrain : ScriptComponent
{
    [ObjectRef] [Tooltip("プレハブの中の子（Body）")]
    GameObject@ body;
}
