#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"

#include <optional>
#include <string>
#include <vector>

namespace CoreEngine
{
/// @brief 読み込むアニメーションクリップ 1 件分の指定
struct AnimationClipDesc {
    /// エンジン内で使う識別名（`Switch()` に渡す名前）
    std::string name;
    /// ファイル内のアニメーション名（空 = 先頭の 1 本）
    /// @note Fox.gltf のように 1 ファイルへ複数アニメーションが入っている場合に使う
    std::string sourceName;
    /// 読み込み元ファイル（空 = モデルファイルと同じ）
    std::string file;
};

/// @brief スケルトンアニメーションの駆動（ロード・前進・クリップ切替・ジョイント行列・骨デバッグ描画）。
/// @details コンストラクタでモデルとクリップを受け取り、`Awake()` でクリップを読んで
///          兄弟 `MeshRendererComponent` にスキニングモデルを作らせる。`AnimationPlayer` の
///          実体はその `Model` が持ち、このコンポーネントは操作の側だけを持つ。
class AnimatorComponent : public IComponent {
public:
    /// @brief モデルを指定せずに作る（既にスキニングモデルを持つ兄弟へ後付けする場合）
    AnimatorComponent() = default;

    /// @brief スケルトンモデルとクリップ 1 本を指定して作る
    /// @param modelPath モデルファイル名（例 "walk.gltf"。ディレクトリは AssetDatabase が解決）
    /// @param clipName  クリップの識別名（ファイル内の先頭アニメーションが読まれる）
    AnimatorComponent(std::string modelPath, std::string clipName)
        : modelPath_(std::move(modelPath)), clips_{ AnimationClipDesc{ std::move(clipName), "", "" } } {}

    /// @brief スケルトンモデルと複数クリップを指定して作る（**先頭が初期クリップ**）
    /// @note クリップはスケルトン生成より前に全部読む必要があるため、後から足せない。
    AnimatorComponent(std::string modelPath, std::vector<AnimationClipDesc> clips)
        : modelPath_(std::move(modelPath)), clips_(std::move(clips)) {}

    const char* GetTypeName() const override { return "Animator"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "アニメーション"; }
#endif

    // ===== ライフサイクル =====

    /// @brief クリップを読み、兄弟 `MeshRendererComponent` にスキニングモデルを作らせる
    /// @note モデル未指定（既定コンストラクタ）の場合は何もしない。
    ///       アニメーションはスケルトン生成より**前**に読む必要があるため、
    ///       クリップのロードとモデル生成をここで一続きに行う。
    void Awake() override;

    /// @brief 兄弟のメッシュ描画・トランスフォームを捕まえる
    void Start() override;

    /// @brief アニメーションを 1 フレーム進める
    /// @note `GameObject::Update()`（派生クラスの `OnUpdate()`）より**前**に走る。
    ///       ジョイント追従のような「更新後の姿勢を読む」処理は
    ///       `SkeletonSocketComponent` のように `LateUpdate()` で行うこと
    ///       （`GameObjectManager` が全オブジェクトの Update 完了後にまとめて回すので、
    ///       生成順に依存せず最新の姿勢が読める）。
    void Update() override;

    // ===== クリップ切り替え =====

    /// @brief アニメーションを即座に切り替える
    /// @return 成功したら true
    bool Switch(const std::string& clipName, bool loop = true);

    /// @brief アニメーションをブレンドしながら切り替える
    /// @details 内部では `AnimationBlender` が現在姿勢と切り替え先姿勢をジョイント単位で
    ///          補間する（平行移動・スケールは Lerp、回転は Slerp）。
    bool SwitchWithBlend(const std::string& clipName, float blendDuration = 0.3f, bool loop = true);

    /// @brief 現在再生中のクリップ識別名
    const std::string& GetCurrentClipName() const { return currentClipName_; }

    /// @brief 現在のクリップ名を設定する（初期化時に呼ぶ）
    void SetCurrentClipName(const std::string& name) { currentClipName_ = name; }

    /// @brief 登録済みクリップの識別名を列挙する
    std::vector<std::string> GetClipNames() const;

    // ===== ジョイント参照 =====
    // 骨のデバッグ表示・武器のソケットアタッチ・ジョイント追従パーティクルは
    // すべてこの 3 つを土台にしている。

    /// @brief 再生中のスケルトン（アニメーションを持たない場合は nullptr）
    const Skeleton* GetSkeleton() const;

    /// @brief ジョイントのワールド行列
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    /// @return スケルトンが無い／名前が見つからない場合は std::nullopt
    /// @details ジョイントが持つのはモデルローカルな「スケルトン空間行列」なので、
    ///          オブジェクトのワールド行列を掛けてワールド空間へ持ち上げる。
    std::optional<Matrix4x4> GetJointWorldMatrix(const std::string& jointName) const;

    /// @brief ジョイントのワールド座標
    std::optional<Vector3> GetJointWorldPosition(const std::string& jointName) const;

    // ===== 骨のデバッグ表示 =====

    void SetSkeletonDebugDrawEnabled(bool enabled) { skeletonDebugDrawEnabled_ = enabled; }
    bool IsSkeletonDebugDrawEnabled() const { return skeletonDebugDrawEnabled_; }

    /// @brief スケルトンの親子関係を線で描画する
    void DrawSkeletonDebugLines() const;

private:
    /// @brief 兄弟コンポーネントが未取得なら取りに行く（Start 前に呼ばれた場合の保険）
    void ResolveSiblings() const;

    /// @brief 再生中の AnimationPlayer（無ければ nullptr）
    class AnimationPlayer* GetPlayer() const;

    /// スケルトンモデルのファイル名（空 = 兄弟が既に持っているモデルを使う）
    std::string modelPath_;

    /// 読み込むクリップ（先頭が初期クリップ）
    std::vector<AnimationClipDesc> clips_;

    mutable MeshRendererComponent* renderer_ = nullptr;
    mutable TransformComponent* transform_ = nullptr;

    /// 現在再生中のクリップ識別名
    std::string currentClipName_;

    /// 骨のデバッグ表示フラグ
    bool skeletonDebugDrawEnabled_ = false;
};
}
