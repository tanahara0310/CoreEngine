#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "WorldTransform/WorldTransform.h"
#include "Math/Vector/Vector3.h"

#include <d3d12.h>

namespace CoreEngine
{
/// @brief 3D モデル用の位置・回転・スケール（`WorldTransform` を内包。GPU 定数バッファつき）。
/// @details `ModelGameObject::transform_` はこの中の実体への参照なので、
///          **このコンポーネントは取り外してはいけない**（参照が宙に浮く）。
class TransformComponent : public IComponent, public ITransformSource {
public:
    const char* GetTypeName() const override { return "Transform"; }

    // ===== ITransformSource（ギズモ・インスペクタ・Undo/Redo からの共通入口） =====

    Vector3& Translate() override { return transform_.translate; }
    Vector3& Rotate()    override { return transform_.rotate; }
    Vector3& Scale()     override { return transform_.scale; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "トランスフォーム"; }
#endif

    // ===== ライフサイクル =====

    /// @brief GPU 定数バッファを確保する（デバイスはオーナー経由で自分で取る）
    /// @note 自己完結させているので、素の GameObject に AddComponent するだけで使える。
    void Awake() override;

    /// @brief 毎フレーム、ローカル→ワールド行列を計算して GPU へ転送する
    /// @note `GameObject::Update()` より**前**に走るので、転送済みのワールド行列を
    ///       後から上書きする処理（ソケット追従など）は `LateUpdate()` 側で行う。
    void Update() override { transform_.TransferMatrix(); }

    // ===== GPU リソース =====

    /// @brief 定数バッファを確保する
    /// @note デバイスが要るので `Awake()` では行わない。オーナーの `Initialize()`
    ///       から明示的に呼ぶ（従来 `transform_.Initialize(device)` を書いていた場所）。
    void InitializeGpuResources(ID3D12Device* device) { transform_.Initialize(device); }

    // ===== アクセサ =====

    WorldTransform& Get() { return transform_; }
    const WorldTransform& Get() const { return transform_; }

    // ===== コライダー向けの問い合わせ =====

    /// @brief ワールド空間の位置（親の階層を含む）
    Vector3 GetWorldPosition() const { return transform_.GetWorldPosition(); }

    /// @brief ワールド空間のスケール（親の階層スケールを含む）
    /// @details 行ベクトル規約（p' = p * M）なので各行が基底ベクトル。その長さがスケール。
    ///          `transform_.scale` を直接返すと親の階層スケールを取りこぼす。
    Vector3 GetWorldScale() const;

    // ===== 衝突解決（押し出し） =====

    /// @brief ワールド空間の移動量を受け入れる
    /// @return 常に true（トランスフォームを持つので必ず動ける）
    /// @details 親のワールド行列の逆行列で方向変換してからローカル translate へ足すので、
    ///          親が回転・スケールしていてもズレない。最後にワールド行列を再計算して GPU へ転送する。
    bool ApplyWorldDelta(const Vector3& delta);

private:
    WorldTransform transform_;
};
}
