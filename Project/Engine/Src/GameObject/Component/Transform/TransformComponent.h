#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Reflection/Reflect.h"
#include "WorldTransform/WorldTransform.h"
#include "Math/Vector/Vector3.h"

#include <d3d12.h>

namespace CoreEngine
{
/// @brief 3D モデル用の位置・回転・スケール（`WorldTransform` を内包。GPU 定数バッファつき）。
/// @details `MeshRendererComponent` や水面のオブジェクトがこのコンポーネントを指したまま持つので、
///          **取り外してはいけない**（指す先が宙に浮く）。
class TransformComponent : public IComponent, public ITransformSource {
public:
    const char* GetTypeName() const override { return "Transform"; }

    REFLECT_BEGIN(TransformComponent, "トランスフォーム")
        REFLECT_PROPERTY(transform_.translate, "位置",     p.range = Speed(0.05f))
        REFLECT_PROPERTY(transform_.rotate,    "回転",     p.range = Speed(0.01f))
        REFLECT_PROPERTY(transform_.scale,     "スケール", p.range = Speed(0.01f))
        REFLECT_OBJECT_REF(parent_, "親")
    REFLECT_END()

    // ===== ITransformSource（ギズモ・インスペクタ・Undo/Redo からの共通入口） =====

    Vector3& Translate() override { return transform_.translate; }
    Vector3& Rotate()    override { return transform_.rotate; }
    Vector3& Scale()     override { return transform_.scale; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "トランスフォーム"; }

    /// @brief 位置・回転・スケールの編集 UI
    /// @return 値が変更されたら true
    bool DrawInspector() override;

    /// @brief 回転の単位とワールド位置を添える
    void DrawInspectorExtra() override;
#endif

    /// @brief 書き換わった位置・回転・スケールをワールド行列へ反映する
    /// @note 更新が止まっているとき（再生停止中）でもインスペクタと Undo を効かせるために要る。
    void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

    // ===== ライフサイクル =====

    /// @brief GPU 定数バッファを確保する（デバイスはオーナー経由で自分で取る）
    /// @note 自己完結させているので、素の GameObject に AddComponent するだけで使える。
    void Awake() override;

    /// @brief 毎フレーム、親を引き直してローカル→ワールド行列を計算し、GPU へ転送する
    /// @note `GameObject::Update()` より**前**に走るので、転送済みのワールド行列を
    ///       後から上書きする処理（ソケット追従など）は `LateUpdate()` 側で行う。
    void Update() override { SyncWorldMatrix(); }

    /// @brief 親を引き直してからワールド行列を計算し、GPU へ転送する
    void SyncWorldMatrix();

    // ===== 親子 =====

    /// @brief 親の Transform を付け替える（nullptr で親を外す）
    /// @return 自分自身か自分の子孫を指したときは付け替えずに false
    /// @note ローカルの位置・回転・スケールはそのまま残る。親は保存され、インスペクタでも選べる。
    bool SetParent(TransformComponent* parent);

    /// @brief 親の Transform（無い・見つからないなら nullptr）
    TransformComponent* GetParent() const { return parent_.Get(); }

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
    /// @brief 親を引き、変わっていれば WorldTransform へ渡す（自分や子孫を指していたら親を外す）
    void ApplyParent();

    /// @brief candidate が自分自身か自分の子孫か
    bool IsSelfOrDescendant(const TransformComponent* candidate) const;

    WorldTransform transform_;

    /// @brief 親の Transform（ID で指すので保存でき、親が消えると引けなくなる）
    ObjectRef<TransformComponent> parent_;

    /// @brief 最後に WorldTransform へ渡した親
    const TransformComponent* appliedParent_ = nullptr;

#ifdef USE_IMGUI
    /// @brief ドラッグ開始時の値（Undo/Redo へ「編集前」として渡す）
    Vector3 editSnapTranslate_{};
    Vector3 editSnapRotate_{};
    Vector3 editSnapScale_{};
    bool    editSnapActive_ = true;
#endif
};
}
