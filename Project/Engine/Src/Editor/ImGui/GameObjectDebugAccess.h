#pragma once

#include "GameObject/GameObject.h"
#include "GameObject/Component/ITransformSource.h"

namespace CoreEngine {
    namespace DebugAccess {

        /// @brief デバッグ操作用に GameObject から編集可能トランスフォームを取得するための参照セット
        struct TransformAccess {
            Vector3* translate = nullptr; ///< 位置ベクトルへの参照
            Vector3* rotate = nullptr;    ///< 回転ベクトルへの参照
            Vector3* scale = nullptr;     ///< スケールベクトルへの参照
        };

        /// @brief GameObject が保持する編集可能トランスフォームへの参照を取得する
        /// @param obj 参照取得対象のゲームオブジェクト
        /// @param out 取得結果を格納する参照セット（成功時に各ポインタが有効値に設定される）
        /// @return トランスフォームを解決できた場合 true
        ///
        /// @note **具象型へのダウンキャストは無くなった**（2026-08-07）。以前は
        ///       @code
        ///       if (auto* s = dynamic_cast<SpriteObject*>(obj))    { ...EulerTransform... }
        ///       if (auto* m = dynamic_cast<ModelGameObject*>(obj)) { ...WorldTransform...  }
        ///       return false;   // UIImage と ParticleSystem はここで落ちてギズモが効かなかった
        ///       @endcode
        ///       と書かれており、対応する具象型を足し忘れるとギズモが無言で効かなくなった。
        ///
        ///       今は `ITransformSource` を実装したコンポーネントを引くだけ。
        ///       `ComponentHost::GetComponent<T>()` は `dynamic_cast` で解決するので
        ///       基底インターフェースを指定して引ける。実体が
        ///       `TransformComponent`（3D モデル・GPU 定数バッファつき）でも
        ///       `EulerTransformComponent`（スプライト・パーティクル）でも同じ経路で通る。
        ///
        ///       **副作用**: `ParticleSystem` / `GpuParticleSystem` でもギズモが効くようになった。
        ///       `UIImage` は座標系が違う（アンカー相対の Vector2）ため意図的に対象外。
        inline bool TryGetTransformAccess(GameObject* obj, TransformAccess& out) {
            out = {};
            if (!obj) {
                return false;
            }

            auto* source = obj->GetComponent<ITransformSource>();
            if (!source) {
                return false;
            }

            out.translate = &source->Translate();
            out.rotate = &source->Rotate();
            out.scale = &source->Scale();
            return true;
        }

    } // namespace DebugAccess
} // namespace CoreEngine
