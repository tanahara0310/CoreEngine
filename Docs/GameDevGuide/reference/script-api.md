# スクリプト API

ここに載っているものは、`Application/Assets/Scripts/as.predefined` から機械的に抜き出したものです。**VS Code の補完に出るものと同じ**です。

!!! info "as.predefined はエディタ起動時に作り直されます"
    エンジン側に関数を足すと自動で更新されます。このページが古いと感じたら、エディタを 1 回起動してからこのページを作り直してください。

## グローバル関数

```cpp
Log("ふつうのログ");
Warn("気になること");
Error("まずいこと");
```

## Time::

時間。`DeltaTime()` を掛けないと、フレームレートで速さが変わります。

```cpp
float DeltaTime();
float UnscaledDeltaTime();
float TimeSinceStartup();
float UnscaledTimeSinceStartup();
uint64 FrameCount();
float FixedDeltaTime();
float TimeScale();
bool IsPaused();
void SetTimeScale(float scale);
```

## Physics::

光線・範囲の検索と、レイヤー同士の当たり設定。

```cpp
RaycastHit@ Raycast(const Vector3&in origin, const Vector3&in direction, float maxDistance, int layerMask = -1);
RaycastHit@[]@ RaycastAll(const Vector3&in origin, const Vector3&in direction, float maxDistance, int layerMask = -1);
GameObject@[]@ OverlapSphere(const Vector3&in center, float radius, int layerMask = -1);
GameObject@[]@ OverlapBox(const Vector3&in center, const Vector3&in size, int layerMask = -1);
void SetLayerCollision(CollisionLayer a, CollisionLayer b, bool enabled);
bool GetLayerCollision(CollisionLayer a, CollisionLayer b);
int LayerMask(CollisionLayer layer);
int GetBodyCount();
int GetSleepingCount();
int GetContactCount();
Vector3 get_gravity() property;
void set_gravity(const Vector3&in) property;
```

## Input::

入力。**操作の名前（`InputAction`）で書いてください。**

```cpp
bool IsActionPressed(InputAction, int player = -1);
bool IsActionTriggered(InputAction, int player = -1);
bool IsActionReleased(InputAction, int player = -1);
float GetAxisValue(InputAction, int player = -1);
float GetAxis(InputAction negative, InputAction positive, int player = -1);
Vector2 GetAxis2D(InputAction negativeX, InputAction positiveX, InputAction negativeY, InputAction positiveY, int player = -1);
bool IsGamepadConnected(int player = 0);
int GetConnectedGamepadCount();
Vector2 GetLeftStick(int player = 0);
Vector2 GetRightStick(int player = 0);
float GetLeftTrigger(int player = 0);
float GetRightTrigger(int player = 0);
void SetVibration(float left, float right, int player = 0);
bool IsMouseButtonPressed(MouseButton);
bool IsMouseButtonTriggered(MouseButton);
bool IsMouseButtonReleased(MouseButton);
Vector2 GetMouseDelta();
float GetWheelDelta();
bool IsPointerOverGame();
bool IsKeyPressed(Key);
bool IsKeyTriggered(Key);
bool IsKeyReleased(Key);
```

## Tween::

値をなめらかに動かす。`SetLink(owner)` を付けると、消えたときに止まります。

```cpp
TweenHandle To(float from, float to, float duration, TweenFloatSetter@ setter);
TweenHandle To(const Vector2&in from, const Vector2&in to, float duration, TweenVector2Setter@ setter);
TweenHandle To(const Vector3&in from, const Vector3&in to, float duration, TweenVector3Setter@ setter);
TweenHandle To(const Vector4&in from, const Vector4&in to, float duration, TweenVector4Setter@ setter);
TweenHandle Delay(float seconds, TweenCallback@ action);
TweenHandle MoveTo(GameObject@ object, const Vector3&in to, float duration);
TweenHandle ScaleTo(GameObject@ object, const Vector3&in to, float duration);
TweenHandle RotateTo(GameObject@ object, const Vector3&in to, float duration);
int KillById(const string&in id, bool complete = false);
int KillByLink(GameObject@ owner, bool complete = false);
TweenSequence Sequence();
```

## UI::

UI のフォーカス。メニューを閉じるときは `ClearFocus()` を呼びます。

```cpp
void ClearFocus();
bool HasFocus();
```

## Font::

フォントの登録。

```cpp
void Register(const string&in name, const string&in filePath, const string[]&in systemFamilies, const string&in charset = "");
```

## Audio::

音。ふつうは `AudioSource` コンポーネントを使ってください。

```cpp
void PlayOneShot(const string&in path, const PlayParams&in params = PlayParams());
Sound@ PlayScoped(const string&in path, const PlayParams&in params = PlayParams());
void SetBusVolume(AudioBus bus, float volume);
float GetBusVolume(AudioBus bus);
void SetMasterVolume(float volume);
float GetMasterVolume();
void StopAll();
```

## CameraShake::

カメラを揺らす。

```cpp
uint Play(const CameraShakeParams&in params);
uint Play(const CameraShakeParams&in params, const Vector3&in worldOrigin);
uint PlayPreset(const string&in name, float scale = 1.0f);
void Stop(uint handle, float fadeOutSeconds = 0.0f);
void StopAll(float fadeOutSeconds = 0.0f);
void AddTrauma(float amount);
```

## CameraShakePresets::

揺れの見本。`CameraShake::PlayPreset("Hit")` でも呼べます。

```cpp
CameraShakeParams Hit();
CameraShakeParams HeavyHit();
CameraShakeParams Explosion();
CameraShakeParams Landing();
CameraShakeParams Recoil();
CameraShakeParams Earthquake();
CameraShakeParams Handheld();
CameraShakeParams Rumble();
```

## Scene::

シーンの切り替え。

```cpp
void ChangeScene(const string&in name);
string GetCurrentName();
GameCamera@ GetGameCamera();
bool IsUsingEditorCamera();
```

## Rendering::

描画の一時的な操作（暗転など）。

```cpp
float GetAutoExposureEV();
void SetFadeAlpha(float alpha);
```

## Session::

**シーンをまたいで残る**入れ物。アプリを閉じると消えます。

```cpp
void SetInt(const string&in key, int value);
int GetInt(const string&in key, int fallback = 0);
void SetFloat(const string&in key, float value);
float GetFloat(const string&in key, float fallback = 0.0f);
void SetBool(const string&in key, bool value);
bool GetBool(const string&in key, bool fallback = false);
void SetString(const string&in key, const string&in value);
string GetString(const string&in key, const string&in fallback = "");
bool Has(const string&in key);
void Remove(const string&in key);
```

## Random::

乱数。

```cpp
float Range(float minimum, float maximum);
int Range(int minimum, int maximum);
bool Chance(float probability = 0.5f);
```

## CVar::

**次回の起動でも残る**設定値。

```cpp
bool Exists(const string&in name);
bool Reset(const string&in name);
bool GetBool(const string&in name, bool fallback = false);
int GetInt(const string&in name, int fallback = 0);
float GetFloat(const string&in name, float fallback = 0.0f);
Vector2 GetVector2(const string&in name);
Vector3 GetVector3(const string&in name);
Vector4 GetColor(const string&in name);
bool SetBool(const string&in name, bool value);
bool SetInt(const string&in name, int value);
bool SetFloat(const string&in name, float value);
bool SetVector2(const string&in name, const Vector2&in value);
bool SetVector3(const string&in name, const Vector3&in value);
bool SetColor(const string&in name, const Vector4&in value);
```

## 列挙

### EaseType

`Linear` `EaseInQuad` `EaseOutQuad` `EaseInOutQuad` `EaseInCubic` `EaseOutCubic` `EaseInOutCubic` `EaseInQuart` `EaseOutQuart` `EaseInOutQuart` `EaseInQuint` `EaseOutQuint` `EaseInOutQuint` `EaseInSine` `EaseOutSine` `EaseInOutSine` `EaseInExpo` `EaseOutExpo` `EaseInOutExpo` `EaseInCirc` `EaseOutCirc` `EaseInOutCirc` `EaseInBack` `EaseOutBack` `EaseInOutBack` `EaseInElastic` `EaseOutElastic` `EaseInOutElastic` `EaseInBounce` `EaseOutBounce` `EaseInOutBounce`

### CollisionLayer

`Default` `Player` `Enemy` `PlayerBullet` `EnemyBullet` `Boss` `BossBullet` `BossAttack` `Item` `Environment`

### InputAction

`MoveForward` `MoveBack` `MoveLeft` `MoveRight` `Jump` `Sprint` `Attack` `Interact` `UINavigateUp` `UINavigateDown` `UINavigateLeft` `UINavigateRight` `UIConfirm` `UICancel` `Pause` `EditorFocusSelection` `EditorGizmoTranslate` `EditorGizmoRotate` `EditorGizmoScale`

### Key

`Num0` `Num1` `Num2` `Num3` `Num4` `Num5` `Num6` `Num7` `Num8` `Num9` `A` `B` `C` `D` `E` `F` `G` `H` `I` `J` `K` `L` `M` `N` `O` `P` `Q` `R` `S` `T` `U` `V` `W` `X` `Y` `Z` `Space` `Enter` `Escape` `Tab` `Shift` `Ctrl` `Left` `Right` `Up` `Down`

### MouseButton

`Left` `Right` `Middle` `XButton1` `XButton2`

### TweenLoop

`Restart` `Yoyo`

### TweenUpdate

`Scaled` `Unscaled`

### UIAnchor

`TopLeft` `TopCenter` `TopRight` `MiddleLeft` `Center` `MiddleRight` `BottomLeft` `BottomCenter` `BottomRight`

### TextAlignH

`Left` `Center` `Right`

### TextAlignV

`Top` `Middle` `Bottom`

### AudioBus

`BGM` `SE` `Voice`

### ShakeWaveform

`Perlin` `Random` `Sine` `Kick`

### ShakeSpace

`CameraLocal` `World`

### ShakeTimeMode

`Scaled` `Unscaled`

