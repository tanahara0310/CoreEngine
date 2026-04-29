#pragma once

#include "UIElement.h"
#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    class EngineSystem;
    class UIRenderer;

    /// @brief UIRenderer への基準解像度設定を担うヘルパー
    /// @details
    ///  UIImage は GameObject として RenderManager が自動管理・描画する。
    ///  UICanvas の唯一の役割は Initialize 時に UIRenderer へ基準解像度を伝えること。
    class UICanvas
    {
    public:
        UICanvas()  = default;
        ~UICanvas() = default;

        /// @brief 初期化（UIRenderer に基準解像度を反映する）
        /// @param engine　EngineSystem ポインタ
        /// @param referenceResolution 基準解像度（{0,0} ならウィンドウサイズに自動追従）
        void Initialize(EngineSystem* engine,
                        const Vector2& referenceResolution = { 0.0f, 0.0f });

        /// @brief 基準解像度を変更する
        void SetReferenceResolution(const Vector2& size);
        Vector2 GetReferenceResolution() const { return referenceResolution_; }

    private:
        EngineSystem* engine_   = nullptr;
        UIRenderer*   renderer_ = nullptr;
        Vector2 referenceResolution_ = { 0.0f, 0.0f };
    };

} // namespace CoreEngine
