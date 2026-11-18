#pragma once

#include "Animation/AnimatorController.h"
#include "Animation/AnimatorRuntime.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace CoreEngine
{
struct Animation;
class Model;

/// @brief 読み込むアニメーションクリップ 1 件分の指定（設定を指さないときに使う）
struct AnimationClipDesc {
    /// エンジン内で使う識別名（`Switch()` に渡す名前）
    std::string name;
    /// ファイル内のアニメーション名（空 = 先頭の 1 本）
    /// @note Fox.gltf のように 1 ファイルへ複数アニメーションが入っている場合に使う
    std::string sourceName;
    /// 読み込み元ファイル（空 = モデルファイルと同じ）
    std::string file;
};

/// @brief スケルトンアニメーションの駆動（状態の切り替え・クリップの再生・ジョイント行列・骨デバッグ描画）。
/// @details 設定（`.animator`）を指していれば、パラメータと遷移の表で状態を切り替える。
///          指していなければ `clips` の 1 本ずつを状態にして、`Switch`・`SwitchWithBlend` で切り替える。
///          `Awake()` で兄弟 `MeshRendererComponent` にスキニングモデルを作らせ、毎フレームの骨の行列をそのモデルへ渡す。
class AnimatorComponent : public IComponent {
public:
    /// @brief モデルを指定せずに作る（兄弟のメッシュ描画のモデルを使う）
    AnimatorComponent() = default;

    /// @brief スケルトンモデルとクリップ 1 本を指定して作る
    /// @param modelPath モデルファイル名（例 "walk.gltf"。ディレクトリは AssetDatabase が解決）
    /// @param clipName  クリップの識別名（ファイル内の先頭アニメーションが読まれる）
    AnimatorComponent(std::string modelPath, std::string clipName)
        : modelPath_(std::move(modelPath)), clips_{ AnimationClipDesc{ std::move(clipName), "", "" } } {}

    /// @brief スケルトンモデルと複数クリップを指定して作る（**先頭が初期クリップ**）
    AnimatorComponent(std::string modelPath, std::vector<AnimationClipDesc> clips)
        : modelPath_(std::move(modelPath)), clips_(std::move(clips)) {}

    const char* GetTypeName() const override { return "Animator"; }

    REFLECT_DECLARE(AnimatorComponent)

    /// @brief スケルトンモデルのファイル名（空 = 兄弟が持っているモデルを使う）
    const std::string& GetModelPath() const { return modelPath_; }

    /// @brief スケルトンモデルのファイル名を差し替える（Awake 済みならモデルを作り直す）
    void SetModelPath(const std::string& modelPath);

    /// @brief 指しているアニメーター設定（何も指していなければ `clips` を使う）
    const AssetRef<AnimatorControllerAsset>& GetController() const { return controller_; }

    /// @brief アニメーター設定をパスかファイル名で指し直す（Awake 済みなら作り直す）
    void SetController(std::string_view pathOrName);

    /// @brief 全体の速さの倍率
    float GetSpeed() const { return speed_; }
    void SetSpeed(float speed) { speed_ = speed; }

    /// @brief クリップの移動でオブジェクトを動かすか（兄弟にキャラクターコントローラーがあればその移動で動かす）
    bool GetApplyRootMotion() const { return applyRootMotion_; }
    void SetApplyRootMotion(bool apply) { applyRootMotion_ = apply; }

    /// @brief 読み込むクリップを書き出す（識別名・ファイル内の名前・読み込み元ファイル）
    void SaveClipsToJson(json& parameters) const;

    /// @brief 読み込むクリップを読む
    void LoadClipsFromJson(const json& parameters);

    // ===== ライフサイクル =====

    /// @brief 兄弟 `MeshRendererComponent` にスキニングモデルを作らせる（最初の状態の最初のコマの姿勢で描く）
    void Awake() override;

    /// @brief 兄弟のメッシュ描画・トランスフォームを捕まえる
    void Start() override;

    /// @brief 兄弟のメッシュ描画とトランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override
    {
        return IsAnyOf<MeshRendererComponent, TransformComponent>(other);
    }

    /// @brief 設定か model を指し直したら作り直す
    void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

    /// @brief アニメーションを 1 フレーム進める
    /// @note `GameObject::Update()` より前に走る。更新後の姿勢を読む処理は
    ///       `SkeletonSocketComponent` のように `LateUpdate()` で行うこと。
    void Update() override;

    // ===== パラメータ（設定を指しているとき） =====

    void SetFloat(const std::string& name, float value);
    float GetFloat(const std::string& name);
    void SetInt(const std::string& name, int value);
    int GetInt(const std::string& name);
    void SetBool(const std::string& name, bool value);
    bool GetBool(const std::string& name);
    void SetTrigger(const std::string& name);
    void ResetTrigger(const std::string& name);

    // ===== 状態 =====

    /// @brief 状態をすぐ切り替える
    void Play(const std::string& stateName);

    /// @brief 混ぜながら状態を切り替える
    void CrossFade(const std::string& stateName, float seconds);

    /// @brief 今の状態の名前（遷移の途中なら移る先）
    const std::string& GetCurrentState() const;

    bool IsInState(const std::string& stateName) const;

    /// @brief 今の状態の進み（ループの中の 0〜1）
    float GetNormalizedTime() const;

    bool IsInTransition() const;

    // ===== レイヤー（名前で指す） =====

    /// @brief レイヤーの重み（0〜1）を変える（最初のレイヤーは 1 のまま）
    void SetLayerWeight(const std::string& layer, float weight);
    float GetLayerWeight(const std::string& layer);

    /// @brief レイヤーの今の状態の名前（遷移の途中なら移る先）
    const std::string& GetLayerState(const std::string& layer);
    bool IsInLayerState(const std::string& layer, const std::string& stateName);

    /// @brief レイヤーの状態をすぐ切り替える
    void PlayInLayer(const std::string& layer, const std::string& stateName);

    /// @brief レイヤーの状態を混ぜながら切り替える
    void CrossFadeInLayer(const std::string& layer, const std::string& stateName, float seconds);

    /// @brief レイヤーの今の状態の進み（ループの中の 0〜1）
    float GetLayerNormalizedTime(const std::string& layer);

    // ===== クリップ切り替え（設定を指していないときの名前。指していれば状態の名前として扱う） =====

    /// @brief アニメーションを即座に切り替える
    /// @return 成功したら true
    bool Switch(const std::string& clipName, bool loop = true);

    /// @brief アニメーションをブレンドしながら切り替える（平行移動・スケールは Lerp、回転は Slerp）
    /// @note ブレンドの途中で呼ぶと、その時の姿勢から混ぜ直す。
    bool SwitchWithBlend(const std::string& clipName, float blendDuration = 0.3f, bool loop = true);

    /// @brief 現在再生中のクリップ識別名（設定を指していれば状態の名前）
    const std::string& GetCurrentClipName() const { return GetCurrentState(); }

    /// @brief 使えるクリップの識別名を列挙する（設定を指していればその clips の id）
    std::vector<std::string> GetClipNames() const;

    // ===== ジョイント参照 =====

    /// @brief 骨の並び（名前と親子。アニメーションを持たない場合は nullptr）
    const Skeleton* GetSkeleton() const;

    /// @brief 骨ごとのスケルトン空間の行列（最後に作った姿勢。並びは GetSkeleton の joints と同じ）
    std::span<const Matrix4x4> GetJointMatrices() const;

    /// @brief ジョイントのワールド行列
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    /// @return スケルトンが無い／名前が見つからない場合は std::nullopt
    std::optional<Matrix4x4> GetJointWorldMatrix(const std::string& jointName) const;

    /// @brief ジョイントのワールド座標
    std::optional<Vector3> GetJointWorldPosition(const std::string& jointName) const;

    // ===== 骨のデバッグ表示 =====

    void SetSkeletonDebugDrawEnabled(bool enabled) { skeletonDebugDrawEnabled_ = enabled; }
    bool IsSkeletonDebugDrawEnabled() const { return skeletonDebugDrawEnabled_; }

    /// @brief スケルトンの親子関係を線で描画する
    void DrawSkeletonDebugLines() const;

    // ===== エディタ =====

    /// @brief 実行の状態（窓が今の状態とパラメータを映す・パラメータを書き換える）
    AnimatorRuntime& GetRuntime() const;

    /// @brief 渡した骨の行列で描かせる（停止中に窓の「シーンで見る」が使う）
    /// @param matrices 骨ごとのスケルトン空間の行列（GetSkeleton の joints と同じ数）
    /// @return 描かせられたら true
    bool ShowPreviewPose(std::span<const Matrix4x4> matrices);

    /// @brief ShowPreviewPose をやめて、実行の状態の姿勢へ戻す
    void ClearPreviewPose();

private:
    /// @brief 兄弟コンポーネントが未取得なら取りに行く（Start 前に呼ばれた場合の保険）
    void ResolveSiblings() const;

    /// @brief 兄弟のメッシュ描画にスキニングモデルを作らせる
    void ApplyToRenderer();

    /// @brief 使う設定（設定を指していなければ clips から作ったもの。無ければ nullptr）
    std::shared_ptr<const AnimatorControllerData> ResolveController() const;

    /// @brief clips から設定を作る（クリップ 1 本ずつを状態にする）
    std::shared_ptr<const AnimatorControllerData> MakeClipController() const;

    /// @brief クリップのファイルが空のときに読むモデル
    std::string ClipModelPath() const;

    /// @brief 設定の最初の状態のクリップ
    std::shared_ptr<const Animation> AcquireInitialClip(const AnimatorControllerData& controller) const;

    /// @brief 兄弟のモデルか設定が変わっていれば、実行の状態を作り直す
    void EnsureBound() const;

    /// @brief このフレームに出たイベントを同じオブジェクトのコンポーネントへ配る
    void DispatchEvents();

    /// @brief このフレームのクリップの移動でオブジェクトを動かす
    void ApplyRootMotion();

    /// @brief Switch / SwitchWithBlend に渡す「ループの指定」（設定を指していれば状態の設定のまま）
    std::optional<bool> LoopOverride(bool loop) const;

    /// スケルトンモデルのファイル名（空 = 兄弟が既に持っているモデルを使う）
    std::string modelPath_;

    /// 読み込むクリップ（先頭が初期クリップ）
    std::vector<AnimationClipDesc> clips_;

    /// アニメーター設定
    AssetRef<AnimatorControllerAsset> controller_;

    /// 全体の速さの倍率
    float speed_ = 1.0f;

    /// クリップの移動でオブジェクトを動かすか
    bool applyRootMotion_ = false;

    mutable MeshRendererComponent* renderer_ = nullptr;
    mutable TransformComponent* transform_ = nullptr;

    mutable AnimatorRuntime runtime_;
    mutable Model* boundModel_ = nullptr;
    mutable const Skeleton* boundSkeleton_ = nullptr;
    mutable std::shared_ptr<const AnimatorControllerLibrary::Entry> controllerEntry_;
    mutable uint64_t boundRevision_ = 0;
    mutable bool bound_ = false;
    mutable std::shared_ptr<const AnimatorControllerData> clipController_;
    std::vector<AnimationEvent> dispatching_;

    bool awoken_ = false;
    bool previewing_ = false;

    /// 骨のデバッグ表示フラグ
    bool skeletonDebugDrawEnabled_ = false;
};
}
