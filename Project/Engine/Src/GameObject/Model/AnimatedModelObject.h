#pragma once

#include "GameObject/Model/ModelGameObject.h"
#include "GameObject/Component/AnimatorComponent.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine {

    /// @brief 読み込むアニメーションクリップ 1 件分の指定
    struct AnimationClipDesc {
        /// エンジン内で使う識別名（切り替え時に指定する名前）
        std::string name;
        /// ファイル内のアニメーション名（空 = 先頭の 1 本）
        /// @note Fox.gltf のように 1 ファイルへ複数アニメーションが入っている場合に使う
        std::string sourceName;
        /// 読み込み元ファイル（空 = モデルファイルと同じ）
        std::string file;
    };

    /// @brief スケルトンアニメーション付き 3D モデルの中間基底クラス
    ///
    /// @details **実体は `AnimatorComponent` が持つ**。このクラスはアセットのロード手順
    ///          （クリップ登録 → スケルトンモデル生成）をテンプレートメソッドで
    ///          まとめているだけの薄いシムで、アニメーション操作 API はすべて
    ///          コンポーネントへ転送している。
    ///
    ///          新しく書くコードは継承ではなく
    ///          `AddComponent<AnimatorComponent>()` を使う。このクラスは
    ///          既存の `WalkModelObject` / `FoxObject` / `BrainStemObject` を
    ///          動かし続けるために残してある。
    ///
    /// 使用例:
    /// @code
    /// class WalkModelObject : public CoreEngine::AnimatedModelObject {
    /// protected:
    ///     std::string GetModelPath()     const override { return "walk.gltf"; }
    ///     std::string GetAnimationName() const override { return "walkAnimation"; }
    /// public:
    ///     const char* GetObjectName()    const override { return "WalkModel"; }
    /// };
    /// @endcode
    class AnimatedModelObject : public ModelGameObject {
    public:
        /// @brief コンストラクタ（アニメーションコンポーネントをアタッチする）
        /// @note `ModelGameObject` が Transform → MeshRenderer をアタッチした後に走るので、
        ///       `AnimatorComponent` は 3 番目。`Sibling<MeshRendererComponent>()` が引ける。
        AnimatedModelObject()
            : animator_(AddComponent<AnimatorComponent>()) {}

        /// @brief 初期化処理（アニメーションロード → スケルトンモデル生成）
        /// @note ModelGameObject::Initialize() の代わりに呼ばれる
        void Initialize() override;

        /// @brief スキニングモデル用の描画パスタイプを返す
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }

        /// @brief アニメーションコンポーネントを取得する
        /// @note ソケット追従（`SkeletonSocketComponent::Attach`）に渡すのはこれ。
        AnimatorComponent* GetAnimator() const { return animator_; }

        // ========== ジョイント参照（AnimatorComponent への転送） ==========

        /// @brief 再生中のスケルトンを取得する
        const Skeleton* GetSkeleton() const { return animator_->GetSkeleton(); }

        /// @brief ジョイントのワールド行列を取得する
        /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
        std::optional<Matrix4x4> GetJointWorldMatrix(const std::string& jointName) const {
            return animator_->GetJointWorldMatrix(jointName);
        }

        /// @brief ジョイントのワールド座標を取得する
        std::optional<Vector3> GetJointWorldPosition(const std::string& jointName) const {
            return animator_->GetJointWorldPosition(jointName);
        }

        // ========== アニメーション切り替え（AnimatorComponent への転送） ==========

        /// @brief アニメーションを即座に切り替える
        bool SwitchAnimation(const std::string& clipName, bool loop = true) {
            return animator_->Switch(clipName, loop);
        }

        /// @brief アニメーションをブレンドしながら切り替える
        bool SwitchAnimationWithBlend(const std::string& clipName, float blendDuration = 0.3f, bool loop = true) {
            return animator_->SwitchWithBlend(clipName, blendDuration, loop);
        }

        /// @brief 現在再生中のクリップ識別名を取得する
        const std::string& GetCurrentClipName() const { return animator_->GetCurrentClipName(); }

        /// @brief 登録済みクリップの識別名を列挙する
        std::vector<std::string> GetAnimationClipNames() const { return animator_->GetClipNames(); }

        // ========== 骨のデバッグ表示（AnimatorComponent への転送） ==========

        void SetSkeletonDebugDrawEnabled(bool enabled) { animator_->SetSkeletonDebugDrawEnabled(enabled); }
        bool IsSkeletonDebugDrawEnabled() const { return animator_->IsSkeletonDebugDrawEnabled(); }

#ifdef USE_IMGUI
        /// @brief インスペクタータブ定義を返す（基底のタブ＋「アニメーション」タブ）
        int GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;

        /// @brief 指定タブのコンテンツを描画する
        bool DrawInspectorTabContent(int tabIndex) override;
#endif

    protected:
        // ========== テンプレートメソッドフック ==========

        /// @brief アニメーション識別名を返す（必須オーバーライド）
        virtual std::string GetAnimationName() const = 0;

        /// @brief アニメーションファイル名を返す（省略時はモデルファイルと同じ）
        virtual std::string GetAnimationFile() const { return GetModelPath(); }

        /// @brief 最初に再生するクリップの、ファイル内アニメーション名を返す（空 = 先頭の 1 本）
        virtual std::string GetSourceAnimationName() const { return ""; }

        /// @brief 追加で読み込むアニメーションクリップを返す（既定は無し）
        /// @note GetAnimationName() のクリップは自動で読み込まれるので、ここには含めない。
        ///       複数クリップを登録すると SwitchAnimationWithBlend() で切り替えられる。
        virtual std::vector<AnimationClipDesc> GetAdditionalAnimationClips() const { return {}; }

        /// @brief Update() 内で呼ばれる
        /// @note アニメーションの前進は `AnimatorComponent::Update()` が行い、それは
        ///       `GameObject::Update()` より前に走る。したがってここに来た時点で
        ///       スケルトンは今フレームの姿勢になっている。
        void OnUpdate() override;

        /// @brief アニメーション更新後に呼ばれる（派生クラスのジョイント追従処理用）
        /// @note この時点でスケルトンは最新の姿勢に更新されているため、
        ///       GetJointWorldMatrix() が今フレームの正しい値を返す。
        ///
        ///       **他オブジェクトのジョイントへ追従させたい場合は
        ///       `SkeletonSocketComponent` を使うこと**。あちらは `LateUpdate()` で動くので
        ///       追従元の生成順に依存しない。
        virtual void OnAnimationUpdated() {}

    private:
        /// アニメーションの実体を持つコンポーネント（コンストラクタでアタッチ済み・非 nullptr）
        AnimatorComponent* animator_ = nullptr;

#ifdef USE_IMGUI
        /// @brief 「アニメーション」タブの中身を描画する
        bool DrawAnimationSection();

        /// インスペクターのブレンド時間スライダーの値 [秒]
        float imguiBlendDuration_ = 0.3f;
#endif
    };

}  // namespace CoreEngine
