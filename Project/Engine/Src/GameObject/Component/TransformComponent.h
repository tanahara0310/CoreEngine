#pragma once

#include "GameObject/Component/IComponent.h"
#include "GameObject/Component/ITransformSource.h"
#include "WorldTransform/WorldTransform.h"
#include "Math/Vector/Vector3.h"

#include <d3d12.h>

namespace CoreEngine
{
/// @brief 位置・回転・スケールを持つコンポーネント
///
/// @details 実体は従来からある `WorldTransform` をそのまま内包している。
///          **意図的に POD にしていない**: `WorldTransform` はオブジェクトごとの
///          D3D12 定数バッファを抱えており（`ConstantBufferDataWorldTransform` を
///          Map したまま保持し、`Model::Draw` がその GPU 仮想アドレスを直接引く）、
///          剥がすと描画経路まで波及する。ここではラップするだけに留める。
///
///          **移行方針**: `ModelGameObject` は `transform_` を「このコンポーネントが
///          持つ WorldTransform への参照」として公開し続ける。したがって
///          `GetTransform()` の戻り値型も `WorldTransform&` のままで、
///          呼び出し側（59 箇所）は 1 行も変わらない。
///
///          参照が安定なのは `ComponentHost` がコンポーネントを個別ヒープへ確保して
///          いるため（追加・削除しても既存の参照は無効化されない）。
///          ただし **このコンポーネントだけは取り外してはいけない**
///          （`ModelGameObject::transform_` の参照が宙に浮く）。
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

    /// @brief 毎フレーム、ローカル→ワールド行列を計算して GPU へ転送する
    /// @note `GameObject::Update()`（従来の派生クラス処理）より**前**に走る。
    ///       これは従来 `ModelGameObject::Update()` が
    ///       `TransferMatrix() → OnUpdate()` の順だったのと同じ順序であり、
    ///       `WeaponObject::OnUpdate()` のように「行列転送の後にワールド行列を
    ///       上書きする」既存コードがそのまま動く。
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
    /// @param delta ワールド空間での移動量
    /// @return 常に true（トランスフォームを持つので必ず動ける）
    /// @details **親を持つ場合もワールド量として正しく動く**。delta を親のワールド行列の
    ///          逆行列で方向変換してからローカル `translate` へ足すため、親が回転・
    ///          スケールしていても押し出し量がズレない。
    ///          （以前の `ModelGameObject::TryApplyCollisionPush` はワールド量を
    ///          ローカル translate へ素で加算していたので、親の回転・スケールを
    ///          無視していた ―― Phase 4 時点の既知の制限）
    ///
    ///          同一フレーム内の後続ペアが新しい位置で判定されるよう、
    ///          最後にワールド行列を再計算して GPU へ転送する。
    bool ApplyWorldDelta(const Vector3& delta);

private:
    WorldTransform transform_;
};
}
