using namespace CoreEngine;

// 操作の読み取り。キーボードとパッドに加えて、スクリプトから差し込んだ操作も押した扱いにする
// （差し込みは試験の自動操作用。普段は何も入らない）

array<bool> gInjectedPressed;
array<bool> gInjectedTriggered;

// 操作を差し込む（pressed は押している間、triggered は押した瞬間のフレームだけ true にする）
void InjectAction(InputAction action, bool pressed, bool triggered)
{
    const uint index = uint(action);
    if (gInjectedPressed.length() <= index) {
        gInjectedPressed.resize(index + 1);
        gInjectedTriggered.resize(index + 1);
    }
    gInjectedPressed[index] = pressed;
    gInjectedTriggered[index] = triggered;
}

bool ActionPressed(InputAction action)
{
    const uint index = uint(action);
    if (index < gInjectedPressed.length() && gInjectedPressed[index]) {
        return true;
    }
    return Input::IsActionPressed(action);
}

bool ActionTriggered(InputAction action)
{
    const uint index = uint(action);
    if (index < gInjectedTriggered.length() && gInjectedTriggered[index]) {
        return true;
    }
    return Input::IsActionTriggered(action);
}
