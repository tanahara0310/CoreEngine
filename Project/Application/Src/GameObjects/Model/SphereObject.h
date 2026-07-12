#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"

/// @brief Sphereモデルオブジェクト
    class SphereObject : public CoreEngine::ModelGameObject {
    public:
        /// @brief オブジェクト名を取得
        const char* GetObjectName() const override { return "Sphere"; }

        /// @brief デフォルトカラーを設定（衝突終了時に復帰するカラー）
        /// @param color カラー（RGBA）
        void SetMaterialColor(const CoreEngine::Vector4& color);

        /// @brief 衝突時のカラーを設定
        /// @param color 衝突中に表示されるカラー（デフォルト: 赤）
        void SetHitColor(const CoreEngine::Vector4& color) { hitColor_ = color; }

        /// @brief 衝突開始イベント
        void OnCollisionEnter(GameObject* other) override;

        /// @brief 衝突中イベント
        void OnCollisionStay(GameObject* other) override;

        /// @brief 衝突終了イベント
        void OnCollisionExit(GameObject* other) override;

    protected:
        std::string GetModelPath() const override { return "SampleAssets/Sphere/sphere.obj"; }
        std::string GetTexturePath() const override { return "white1x1.png"; }


    private:
        CoreEngine::Vector4 defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        CoreEngine::Vector4 hitColor_ = { 1.0f, 0.2f, 0.2f, 1.0f };
        int hitCount_ = 0;  // 同時衝突数（複数衝突でも正しく元の色に戻すため）
    };

