#pragma once

#include "Graphics/Cloud/Render/CloudNoiseBaker.h"
#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/Cloud/Render/CloudRenderer.h"
#include "Graphics/Cloud/Render/CloudSkyCubemapBaker.h"
#include "Graphics/Cloud/Render/GodRayRenderer.h"
#include "Graphics/Cloud/Resource/CloudResources.h"
#include "Graphics/Cloud/Settings/CloudSettings.h"
#include "Graphics/Cloud/Shader/CloudPipelines.h"
#include "Graphics/Cloud/Shader/CloudShaderConstants.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Math/MathCore.h"

#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class AtmosphereManager;
    class DescriptorAllocator;
    class GraphicsCore;

    /// @brief ボリューメトリック雲システムの窓口
    /// @details 設定・リソース・パイプライン・各レンダラーを所有し、フレーム状態を定数バッファへ
    ///          詰めて記録を各レンダラーへ委譲する。太陽情報は AtmosphereManager から取得する。
    class VolumetricCloudManager {
    public:
        /// @brief 初期化
        /// @param graphicsCore デバイスと、ターゲット再確保前の GPU 完了待ちの取得元
        /// @param descriptorAllocator ノイズ SRV/UAV 登録先（メインのシェーダー可視ヒープ）
        void Initialize(GraphicsCore* graphicsCore, DescriptorAllocator* descriptorAllocator);

        /// @brief フレーム更新（雲を使うシーンの BaseScene::UpdateAtmosphere から呼ばれる）
        /// @details cloudsActive_ を立て、カメラ・時刻・太陽情報を CB へ反映する。
        /// @param atmosphereManager 太陽情報・カメラ高度の取得元（AtmosphereManager と同一の値）
        void Update(const Vector3& cameraWorldPosition,
            const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
            const AtmosphereManager* atmosphereManager,
            float deltaTimeSec);

        // ===== パラメータ =====

        /// @brief 現在のパラメータ（実体は CloudCVars。Update が毎フレーム取り込む）
        /// @note 変更するときは CloudCVars 側を Set すること（ここへ書いても次の Update で戻る）
        const VolumetricCloudParameters& GetParameters() const { return parameters_; }

        // ===== 機能トグル =====

        /// @brief 雲描画の有効/無効（既定 true。大気シーンで雲だけ OFF にできる）
        void SetEnabled(bool enabled);
        bool IsEnabled() const { return enabled_; }

        // ===== フレーム有効化（AtmosphereManager と同一パターン） =====

        /// @brief このフレームで雲が要求されているか（Update() が呼ばれ、かつ enabled_）
        bool AreCloudsActive() const { return cloudsActive_ && enabled_; }

        /// @brief フレーム終端で有効化フラグをリセットする（EngineSystem が全 View 描画後に呼ぶ）
        void ResetFrameActivation() { cloudsActive_ = false; }

        // ===== ノイズ生成（VolumetricCloudNoisePass から毎フレーム呼ばれる） =====

        /// @brief ダーティ時のみノイズテクスチャ群を再生成する
        /// @param cmdList 記録先コマンドリスト
        void GenerateNoiseTexturesIfNeeded(ID3D12GraphicsCommandList* cmdList);

        /// @brief ノイズテクスチャが生成済みか
        bool AreNoiseTexturesReady() const { return noiseBaker_.IsReady(); }

        // ===== 雲描画（VolumetricCloudPass から呼ばれる） =====

        /// @brief 雲をレイマーチして SceneColor へ合成する
        /// @param sceneColor SceneColor（実体＋現在ステート）
        void RenderClouds(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& sceneColor,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
            const AtmosphereManager* atmosphereManager);

        // ===== 空キューブマップへの雲焼き込み（AtmosphereLUTPass から呼ばれる） =====

        /// @brief 空キューブマップへ雲を前乗算合成する（スペキュラ IBL / 水面の雲反射用）
        /// @warning CaptureSkyEnvironment の直後・PrefilterSkyEnvironment の前に呼ぶこと
        ///          （キューブマップは UAV 状態が前提）
        void RenderCloudsToSkyCubemap(
            ID3D12GraphicsCommandList* cmdList,
            const AtmosphereManager* atmosphereManager);

        // ===== ゴッドレイ（GodRayPass から呼ばれる） =====

        /// @brief 雲シャドウマップ生成 → ゴッドレイマーチ → SceneColor 合成
        /// @details 合成モデルは差分法（遮蔽あり − 遮蔽なし ≤ 0 を加算）。
        ///          既存の Sky-View / Aerial Perspective が加算済みの「遮蔽なし内散乱」との
        ///          二重加算を避けつつ、雲影の空気柱を暗くして光芒の明暗対比を作る。
        void RenderGodRays(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& sceneColor,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
            const AtmosphereManager* atmosphereManager);

    private:
        /// @brief 現在のパラメータ・カメラ・太陽情報から定数バッファを更新する
        void UploadConstants();

        /// @brief ゴッドレイ CB を現在のカメラ・パラメータで更新する
        void UploadGodRayConstants();

        /// @brief フレームターゲットを現在の SceneColor サイズと分割数で確保する
        bool EnsureFrameTargets(GpuResource& sceneColor);

        /// @brief 各レンダラーへ渡す参照一式を組み立てる
        CloudRenderContext MakeRenderContext(
            ID3D12GraphicsCommandList* cmdList, const AtmosphereManager* atmosphereManager);

        VolumetricCloudParameters parameters_{};

        // フレーム更新で計算される値
        Vector3 cameraWorldPos_{};
        Matrix4x4 invViewProj_{};
        Vector3 sunDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector3 sunColor_ = { 1.0f, 1.0f, 1.0f };
        float sunIntensity_ = 1.0f;
        Vector3 moonDirection_ = { 0.0f, -1.0f, 0.0f };
        Vector3 moonColor_ = { 1.0f, 1.0f, 1.0f };
        float moonIntensity_ = 0.0f;
        bool hasMoon_ = false;
        /// 雲層シェルの基準。大気と同じ値を使う（AtmosphereParameters の既定で初期化）
        float planetRadiusM_ = 6360000.0f;
        float groundLevelY_ = 0.0f;
        float timeSec_ = 0.0f;

        // 状態フラグ
        bool enabled_ = true;           ///< 機能トグル（既定 true）
        bool cloudsActive_ = false;     ///< このフレームで Update() が呼ばれ雲が要求されたか
        bool noisePipelinesReady_ = false;  ///< ノイズ生成パイプライン構築済みか
        bool pipelinesReady_ = false;       ///< レイマーチ/合成パイプライン構築済みか
        bool godRayPipelinesReady_ = false; ///< ゴッドレイパイプライン構築済みか

        GraphicsCore* graphicsCore_ = nullptr;
        ID3D12Device* device_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        // 定数バッファ（永続マップ）
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        VolumetricCloudShaderConstants* constantData_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> godRayConstantBuffer_;
        GodRayShaderConstants* godRayConstantData_ = nullptr;

        CloudResources resources_{};
        CloudPipelines pipelines_{};

        // 各 GPU ジョブの記録担当（状態を持つのはノイズ生成のダーティ管理だけ）
        CloudNoiseBaker noiseBaker_{};
        CloudRenderer cloudRenderer_{};
        CloudSkyCubemapBaker skyCubemapBaker_{};
        GodRayRenderer godRayRenderer_{};
    };
}
