#pragma once

#include "GameObject/Model/ModelGameObject.h"
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
    /// ModelGameObject をさらに特化し、アニメーションのロード・更新を自動化する。
    /// 派生クラスは GetModelPath() / GetAnimationName() をオーバーライドするだけでよい。
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
        /// @brief 初期化処理（アニメーションロード → スケルトンモデル生成）
        /// @note ModelGameObject::Initialize() の代わりに呼ばれる
        void Initialize() override;

        /// @brief スキニングモデル用の描画パスタイプを返す
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }

        // ========== ジョイント参照 ==========
        // 骨のデバッグ表示・武器のソケットアタッチ・ジョイント追従パーティクルは
        // すべてこの 3 つの API を土台にしている。

        /// @brief 再生中のスケルトンを取得する
        /// @return スケルトンへのポインタ（アニメーションを持たない場合は nullptr）
        /// @note 実体はアニメーションコントローラーが所有しており、毎フレーム更新される。
        const Skeleton* GetSkeleton() const;

        /// @brief ジョイントのワールド行列を取得する
        /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
        /// @return ワールド行列。スケルトンが無い／名前が見つからない場合は std::nullopt
        /// @details ジョイントが持つのはモデルローカルな「スケルトン空間行列」なので、
        ///          オブジェクトのワールド行列を掛けてワールド空間へ持ち上げる。
        std::optional<Matrix4x4> GetJointWorldMatrix(const std::string& jointName) const;

        /// @brief ジョイントのワールド座標を取得する
        /// @param jointName ジョイント名
        /// @return ワールド座標。見つからない場合は std::nullopt
        std::optional<Vector3> GetJointWorldPosition(const std::string& jointName) const;

        // ========== アニメーション切り替え ==========

        /// @brief アニメーションを即座に切り替える
        /// @param clipName GetAnimationName() / GetAdditionalAnimationClips() で登録した識別名
        /// @param loop ループ再生するか
        /// @return 成功したら true
        bool SwitchAnimation(const std::string& clipName, bool loop = true);

        /// @brief アニメーションをブレンドしながら切り替える
        /// @param clipName 切り替え先の識別名
        /// @param blendDuration ブレンド時間（秒）
        /// @param loop ループ再生するか
        /// @return 成功したら true
        /// @details 内部では AnimationBlender が現在姿勢と切り替え先姿勢を
        ///          ジョイント単位で補間する（平行移動・スケールは Lerp、回転は Slerp）。
        bool SwitchAnimationWithBlend(const std::string& clipName, float blendDuration = 0.3f, bool loop = true);

        /// @brief 現在再生中のクリップ識別名を取得する
        const std::string& GetCurrentClipName() const { return currentClipName_; }

        /// @brief 登録済みクリップの識別名を列挙する
        std::vector<std::string> GetAnimationClipNames() const;

        // ========== 骨のデバッグ表示 ==========

        /// @brief 骨（スケルトン）のデバッグ表示を切り替える
        void SetSkeletonDebugDrawEnabled(bool enabled) { skeletonDebugDrawEnabled_ = enabled; }

        /// @brief 骨のデバッグ表示が有効か
        bool IsSkeletonDebugDrawEnabled() const { return skeletonDebugDrawEnabled_; }

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

        /// @brief Update() 内で TransferMatrix() の後に呼ばれる（アニメーション更新を含む）
        void OnUpdate() override;

        /// @brief アニメーション更新後に呼ばれる（派生クラスのジョイント追従処理用）
        /// @note この時点でスケルトンは最新の姿勢に更新されているため、
        ///       GetJointWorldMatrix() が今フレームの正しい値を返す。
        virtual void OnAnimationUpdated() {}

    private:
        /// @brief スケルトンの親子関係を線で描画する
        void DrawSkeletonDebugLines() const;

#ifdef USE_IMGUI
        /// @brief 「アニメーション」タブの中身を描画する
        bool DrawAnimationSection();
#endif

        /// 骨のデバッグ表示フラグ
        bool skeletonDebugDrawEnabled_ = false;

        /// 現在再生中のクリップ識別名
        std::string currentClipName_;

#ifdef USE_IMGUI
        /// インスペクターのブレンド時間スライダーの値 [秒]
        float imguiBlendDuration_ = 0.3f;
#endif
    };

}  // namespace CoreEngine
