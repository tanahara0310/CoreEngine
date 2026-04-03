# カメラ管理

CoreEngine のカメラシステムは `CameraManager` で複数のカメラを管理し、動的に切り替えることができます。

## ヘッダ

```cpp
#include "Camera/CameraManager.h"
#include "Camera/Release/Camera.h"
#include "Camera/ICamera.h"
```

## 概要

- `BaseScene` が `CameraManager` を自動生成・管理します
- 3D カメラと 2D カメラの両方をサポートします
- 名前ベースでカメラを登録・切り替えできます
- `BaseScene` のシーンセットアップ時にデフォルトカメラが自動登録されます

## CameraManager

### カメラの登録と切り替え

```cpp
// カメラの作成と登録
auto myCamera = std::make_unique<CoreEngine::Camera>();
myCamera->Initialize(device);
myCamera->SetTranslate({ 0.0f, 10.0f, -20.0f });
myCamera->SetRotate({ 0.3f, 0.0f, 0.0f });

cameraManager_->RegisterCamera("MainCamera", std::move(myCamera));

// アクティブカメラの設定
cameraManager_->SetActiveCamera("MainCamera", CoreEngine::CameraType::Camera3D);

// アクティブカメラの取得
auto* activeCamera = cameraManager_->GetActiveCamera(CoreEngine::CameraType::Camera3D);
```

### カメラ情報の取得

```cpp
// ビュー行列・プロジェクション行列の取得
const Matrix4x4& view = cameraManager_->GetViewMatrix();
const Matrix4x4& proj = cameraManager_->GetProjectionMatrix();

// カメラ位置の取得
Vector3 camPos = cameraManager_->GetCameraPosition();

// アクティブカメラ名の取得
const std::string& name = cameraManager_->GetActiveCameraName(CameraType::Camera3D);
```

## Camera クラス

### 主要メソッド

| メソッド | 説明 |
|---------|------|
| `Initialize(device)` | 初期化 |
| `SetTranslate(pos)` | カメラ位置を設定 |
| `SetRotate(rot)` | カメラ回転を設定（ラジアン） |
| `SetScale(scale)` | カメラスケールを設定 |
| `GetPosition()` | カメラ位置を取得 |
| `GetViewMatrix()` | ビュー行列を取得 |
| `GetProjectionMatrix()` | プロジェクション行列を取得 |

### カメラパラメータ

`CameraParameters` 構造体でカメラの投影パラメータを制御できます:

```cpp
auto* camera = cameraManager_->GetCamera("MainCamera");
CoreEngine::CameraParameters params = camera->GetParameters();
// params の変更後に SetParameters() で反映
camera->SetParameters(params);
```

## CameraManager メソッド一覧

| メソッド | 説明 |
|---------|------|
| `RegisterCamera(name, camera)` | カメラを登録 |
| `UnregisterCamera(name)` | カメラを登録解除 |
| `SetActiveCamera(name, type)` | アクティブカメラを設定 |
| `GetActiveCamera(type)` | アクティブカメラを取得 |
| `GetCamera(name)` | 名前でカメラを取得 |
| `GetViewMatrix()` | アクティブ 3D カメラのビュー行列 |
| `GetProjectionMatrix()` | アクティブ 3D カメラのプロジェクション行列 |
| `GetCameraPosition()` | アクティブ 3D カメラの位置 |
| `Update()` | 全アクティブカメラを更新 |
| `GetCameraCount()` | 登録カメラ数 |
| `GetActiveCameraName(type)` | アクティブカメラ名を取得 |
