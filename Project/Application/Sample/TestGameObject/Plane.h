#pragma once

#include "ObjectCommon/GameObject.h"



class Plane : public CoreEngine::GameObject {
public:

    /// @brief 初期化処理
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Plane"; }

    CoreEngine::Model* GetModel() { return model_.get(); }
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

private:


};



