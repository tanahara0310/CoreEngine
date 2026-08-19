#pragma once

#include "GameObject/Model/ModelGameObject.h"
#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"
#include <memory>
#include <string>

namespace CoreEngine
{
    /// @brief プリミティブメッシュを持つゲームオブジェクトの基底クラス
    /// @details 派生側で CreateMeshGenerator() を実装すると、モデルファイルの代わりに
    ///          そのジェネレーターから形状を生成する。他は ModelGameObject と同じ。
    class PrimitiveGameObject : public ModelGameObject {
    public:
        /// @brief 初期化処理（メッシュ生成・トランスフォーム・マテリアルの自動セットアップ）
        void Initialize() override;

        /// @brief オブジェクト種別名を返す
        const char* GetObjectName() const override;

    protected:
        /// @brief 使用するメッシュジェネレーターを作成して返す
        /// @return ジェネレーターのユニークポインタ
        /// @note 派生クラスで必ずオーバーライドして、目的の形状を返す。
        virtual std::unique_ptr<IPrimitiveMeshGenerator> CreateMeshGenerator() const = 0;

        /// @brief GetModelPath は使用しない（メッシュは CreateMeshGenerator で生成する）
        std::string GetModelPath() const override final;
    };
}
