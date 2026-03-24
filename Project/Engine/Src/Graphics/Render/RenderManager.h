#pragma once

#include "IRenderer.h"
#include "RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Model/ModelRenderContext.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include <d3d12.h>
#include <unordered_map>
#include <vector>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class IDrawable;
    class GameObject;
    class ICamera;
    class CameraManager;
    class ShadowMapManager;
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

    /// @brief シャドウマップマネージャーを設定
    /// @param shadowMapManager シャドウマップマネージャー
    void SetShadowMapManager(ShadowMapManager* shadowMapManager);

    /// @brief ライトビュープロジェクション行列を設定（ShadowMapManagerに委譲）
    /// @param lightViewProjection ライトから見たビュープロジェクション行列
    void SetLightViewProjection(const Matrix4x4& lightViewProjection);

        /// @brief 描画対象オブジェクトをキューに追加
        /// @param obj 描画するGameObject
        void AddDrawable(CoreEngine::GameObject* obj);

    /// @brief シャドウパスのみ描画（描画キュー必須）
    void DrawShadowPass();

    /// @brief G-Bufferパスのみ描画（不透明 Model / SkinnedModel を蓄積）
    void DrawGBufferPass();

    /// @brief 通常ジオメトリパスのみ描画（描画キュー必須）
    void DrawGeometryPass();

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

        /// @brief 不透明の Model/SkinnedModel を GeometryPass でスキップするか設定する
        /// @param skip true: スキップ（DeferredLighting に任せる）/ false: 旧従来 Forwardで描画
        /// @note GBufferPass + DeferredLightingPass が有効な場合に true を設定する
        void SetSkipOpaqueMeshInForwardPass(bool skip) { skipOpaqueModelsInForward_ = skip; }

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

        /// @brief アクティブなトランスフォームスロットを設定（Game=通常パス, Scene=エディタシーンビュー）
        void SetActiveTransformSlot(TransformBufferSlot slot) { activeTransformSlot_ = slot; }

        /// @brief 現在アクティブなトランスフォームスロットを取得
        TransformBufferSlot GetActiveTransformSlot() const { return activeTransformSlot_; }

    private:
        struct DrawCommand {
            CoreEngine::GameObject* object;
            RenderPassType passType;
            BlendMode blendMode;
            int renderOrder;         ///< 描画順序（小さいほど先に描画）
            size_t registrationOrder;
        };

        std::vector<DrawCommand> drawQueue_;
        std::unordered_map<RenderPassType, std::unique_ptr<IRenderer>> renderers_;
        std::unordered_map<RenderPassType, int> passTypePriorities_;  ///< 描画パスタイプごとの描画順序優先度
        size_t registrationCounter_ = 0;
        bool isQueueSorted_ = false;

    // フレームごとに設定されるコンテキスト
    ID3D12GraphicsCommandList* cmdList_ = nullptr;
    CoreEngine::CameraManager* cameraManager_ = nullptr;
    const CoreEngine::ICamera* camera_ = nullptr; // 従来の互換性維持用

    // シャドウマップ関連
    ShadowMapManager* shadowMapManager_ = nullptr;
    bool renderDebugLines_ = true;

    // GBuffer 移行制御
    // true にすると RenderNormalPass で不透明 Model/SkinnedModel をスキップする
    // DeferredLightingPass が有効な場合に使用する
    bool skipOpaqueModelsInForward_ = false;

    // アクティブなトランスフォームスロット
    TransformBufferSlot activeTransformSlot_ = TransformBufferSlot::Game;

    // IBL / Environment関連
    D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_ = {};
    Vector3 iblRotation_ = {}; ///< シーン共通IBL環境回転（ラジアン）

        /// @brief 描画パスごとにソート
        void SortDrawQueue();

        /// @brief 必要な場合のみ描画キューをソート
        void EnsureQueueSorted();

    /// @brief 描画パスタイプに応じた適切なカメラを取得
    /// @param passType 描画パスタイプ
    /// @return カメラポインタ
    const CoreEngine::ICamera* GetCameraForPass(RenderPassType passType);

    /// @brief 通常描画パス
    void RenderNormalPass();

    /// @brief シャドウマップ描画の内部実行
    void RenderShadowMapPass();

    /// @brief PBR対象レンダラーへ環境/IBLを適用
    void ApplyEnvironmentLightingToRenderers();
};
}
