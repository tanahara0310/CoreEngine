#pragma once

#include "IRenderer.h"
#include "RenderItem.h"
#include "RenderPassType.h"
#include "DrawViewInfo.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <d3d12.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

// 前方宣言
namespace CoreEngine {
    class ICamera;
    class CameraManager;
}

/// @brief レンダリング全体を自動管理するマネージャー

namespace CoreEngine
{
    class RenderManager {
    public:
        /// @brief 初期化
        /// @param device D3D12デバイス
        void Initialize(ID3D12Device* device);

        /// @brief レンダラーを登録
        /// @param type 描画パスタイプ
        /// @param renderer レンダラーのユニークポインタ
        void RegisterRenderer(RenderPassType type, std::unique_ptr<IRenderer> renderer);

        /// @brief レンダラーを取得
        /// @param type 描画パスタイプ
        /// @return レンダラーポインタ
        IRenderer* GetRenderer(RenderPassType type);

        /// @brief カメラマネージャーを設定
        /// @param cameraManager カメラマネージャー
        void SetCameraManager(CoreEngine::CameraManager* cameraManager);

        /// @brief アクティブな Camera3D のビュー行列を取得
        const Matrix4x4& GetViewMatrix() const;

        /// @brief アクティブな Camera3D のプロジェクション行列を取得
        const Matrix4x4& GetProjectionMatrix() const;


        /// @brief カメラを設定（従来の互換性維持版）
        /// @param camera カメラオブジェクト
        void SetCamera(const CoreEngine::ICamera* camera);

        /// @brief 環境マップをPBR対象レンダラーへ設定
        /// @param environmentMapHandle 環境マップSRV
        void SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle);

        /// @brief IBLテクスチャ群をPBR対象レンダラーへ設定
        /// @param irradianceHandle Irradiance Map SRV
        /// @param prefilteredHandle Prefiltered Map SRV
        /// @param brdfLUTHandle BRDF LUT SRV
        void SetIBLMaps(
            D3D12_GPU_DESCRIPTOR_HANDLE irradianceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE prefilteredHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle);

        /// @brief コマンドリストを設定（フレームごとに1回）
        /// @param cmdList コマンドリスト
        void SetCommandList(ID3D12GraphicsCommandList* cmdList);

        /// @brief 描画項目をキューに追加
        /// @param item 描画する RenderItem
        void AddRenderItem(RenderItem item);

    // 各 Draw*Pass の viewType はパスが context.viewSettings.viewType から明示的に渡す。
    // 描画オブジェクトへは DrawViewInfo（カメラ・ビュー種別・パス種別）として届く。
    // 省略時は GameView（RenderGraph を経由しないレガシー呼び出し向け）。

    /// @brief G-Bufferパスのみ描画（不透明 Model / SkinnedModel を蓄積）
    void DrawGBufferPass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief 通常ジオメトリパスのみ描画（描画キュー必須）
    void DrawGeometryPass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief 通常 RenderItem キューのみ描画する
    void DrawMainQueuePass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief 水面 RenderItem キューのみ描画する
    void DrawWaterQueuePass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief Sky RenderItem キューのみ描画する
    void DrawSkyQueuePass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief Transparent RenderItem キューのみ描画する
    void DrawTransparentQueuePass(RenderViewType viewType = RenderViewType::GameView);

    /// @brief 描画パスタイプの描画順序優先度を設定（小さいほど先に描画）
    /// @param type 描画パスタイプ
    /// @param priority 優先度値
    void SetPassTypePriority(RenderPassType type, int priority);

    /// @brief 描画パスタイプの描画順序優先度を取得
    /// @param type 描画パスタイプ
    /// @return 現在の優先度値
    int GetPassTypePriority(RenderPassType type) const;

    /// @brief 全パスタイプの優先度をデフォルト値にリセット
    void ResetPassTypePriorities();

    /// @brief フレーム終了時にキューをクリア
    void ClearQueue();

        /// @brief デバッグライン描画の有効/無効を設定
        void SetDebugLineRenderingEnabled(bool enabled) { renderDebugLines_ = enabled; }

        /// @brief Deferred ライティング経路が有効かを設定する
        /// @details true（既定）: 不透明 Model/SkinnedModel は投入時に GBuffer キューへ
        ///          振り分けられ、DeferredLighting で描画される。
        ///          false: Forward 経路にフォールバックし、不透明キューを Forward で描画する
        ///          （デバッグ用途）。
        void SetDeferredLightingActive(bool active) { deferredLightingActive_ = active; }

        /// @brief Irradiance Map の GPU SRV ハンドルを取得（DeferredLightingPass の IBL 接続用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceMapHandle() const { return irradianceMapHandle_; }

        /// @brief Prefiltered Map の GPU SRV ハンドルを取得（DeferredLightingPass の IBL 接続用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredMapHandle() const { return prefilteredMapHandle_; }

        /// @brief BRDF LUT の GPU SRV ハンドルを取得（DeferredLightingPass の IBL 接続用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetBRDFLUTHandle() const { return brdfLUTHandle_; }

        /// @brief シーン共通 IBL 環境回転角度を設定（ラジアン）— SkyBox 回転と連動
        void SetIBLRotation(const Vector3& rotation);

        /// @brief シーン共通 IBL 環境回転角度を取得
        const Vector3& GetIBLRotation() const { return iblRotation_; }

        /// @brief 環境輝度スケールを設定（SkyBox intensity と連動）
        void SetEnvironmentIntensity(float intensity);

        /// @brief 環境輝度スケールを取得
        float GetEnvironmentIntensity() const { return environmentIntensity_; }

    private:
        std::vector<RenderItem> drawQueue_;
        std::vector<RenderItem> opaqueDrawQueue_; ///< Deferred 経路（GBuffer）へ振り分けた不透明 Model/SkinnedModel
        std::vector<RenderItem> skyDrawQueue_;
        std::vector<RenderItem> transparentDrawQueue_;
        std::vector<RenderItem> waterDrawQueue_;
        std::unordered_map<RenderPassType, std::unique_ptr<IRenderer>> renderers_;
        std::unordered_map<RenderPassType, int> passTypePriorities_;  ///< 描画パスタイプごとの描画順序優先度
        size_t registrationCounter_ = 0;
        bool isQueueSorted_ = false;

    // フレームごとに設定されるコンテキスト
    ID3D12GraphicsCommandList* cmdList_ = nullptr;
    CoreEngine::CameraManager* cameraManager_ = nullptr;
    const CoreEngine::ICamera* camera_ = nullptr; // 従来の互換性維持用

    bool renderDebugLines_ = true;

    // Deferred ライティング経路の有効フラグ
    // true: 不透明 Model/SkinnedModel は opaqueDrawQueue_（GBuffer 経路）で描画される
    // false: Forward フォールバック（DrawMainQueuePass が不透明キューも描画する）
        bool deferredLightingActive_ = true;

    // IBL / Environment関連
    D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_ = {};
    Vector3 iblRotation_ = {}; ///< シーン共通IBL環境回転（ラジアン）
    float environmentIntensity_ = 1.0f; ///< 環境輝度スケール（SkyBox intensity と連動）

        /// @brief 描画パスごとにソート
        void SortDrawQueue();

        /// @brief 必要な場合のみ描画キューをソート
        void EnsureQueueSorted();

    /// @brief 描画パスタイプに応じた適切なカメラを取得
    /// @param passType 描画パスタイプ
    /// @return カメラポインタ
    const CoreEngine::ICamera* GetCameraForPass(RenderPassType passType);

    /// @brief 指定キューに対する通常（Forward 系）描画パス
    /// @param viewType 実行中のビュー種別（DrawViewInfo として各オブジェクトへ渡す）
    void RenderNormalPassQueue(const std::vector<RenderItem>& queue, RenderViewType viewType);

    /// @brief 指定キューを sortKey 順にソートする
    void SortRenderQueue(std::vector<RenderItem>& queue);

    /// @brief RenderItem の実効描画順を解決する
    int ResolveRenderOrder(const RenderItem& item) const;

    /// @brief 指定パスに対応するレンダラーを解決する
    IRenderer* ResolveRendererForPass(RenderPassType passType);

    /// @brief PBR対象レンダラーへ環境/IBLを適用
    void ApplyEnvironmentLightingToRenderers();
};
}
