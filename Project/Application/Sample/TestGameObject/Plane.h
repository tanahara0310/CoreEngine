#pragma once

#include "ObjectCommon/GameObject.h"

namespace CoreEngine {


    class Plane : public GameObject {
    public:

        /// @brief 初期化処理
        void Initialize();

        /// @brief 更新処理
        void Update() override;

        /// @brief 描画処理
        /// @param camera カメラ
        void Draw(const ICamera* camera) override;

#ifdef _DEBUG

        const char* GetObjectName() const override { return "Plane"; }

#endif // _DEBUG

        Model* GetModel() { return model_.get(); }
        WorldTransform& GetTransform() { return transform_; }

    private:

        
    };

}

