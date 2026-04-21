#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include "Math/Vector/Vector3.h"
#include "GlobalRootSignatureManager.h"
#include "RayTracingPipelineBuilder.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    /// @brief DXR レイトレーシングシャドウを管理するクラス
    /// @details State Object / Shader Table / UAV テクスチャの作成と DispatchRays を担当
    ///          SceneView / GameView で独立した結果を保持するため、2枚のシャドウテクスチャを持つ
    /// @brief DXRシャドウのパラメータ設定
    struct RayTracingShadowSettings {
        float shadowBias = 0.05f;    ///< セルフシャドウ防止バイアス
        float maxRayDistance = 1000.0f;  ///< シャドウレイの最大射程距離
        float lightRadius = 0.15f;    ///< ソフトシャドウの光源角半径（ラジアン）。0でハードシャドウ
        int   softShadowSamples = 8;        ///< ソフトシャドウのサンプル数（1=ハード、4〜8推奨）
    };

    class RayTracingShadowManager {
    public:
        /// @brief ビュー識別子
        enum class ViewID : uint32_t {
            SceneView = 0,
            GameView = 1,
            Count
        };

        static constexpr uint32_t kViewCount          = static_cast<uint32_t>(ViewID::Count);
        static constexpr uint32_t kMaxDirectionalLights = 4; ///< LightManager::MAX_DIRECTIONAL_LIGHTS と合わせる

        /// @brief 初期化（State Object / Shader Table / UAV テクスチャの構築）
        /// @return 成功した場合 true
        bool Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        /// @brief シャドウレイをディスパッチする
        /// @param lightIndex ディレクショナルライトのインデックス（0〜kMaxDirectionalLights-1）
        void Dispatch(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const Vector3& lightDirection,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 初期化済みか
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 指定ビュー・ライトで今フレームにディスパッチ済みか
        bool IsDispatchedThisFrame(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief フレーム開始時に全ビュー・全ライトの状態をリセット
        void ResetFrameState();

        /// @brief 出力テクスチャを指定サイズで確保する
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief シャドウパラメータを設定する
        void SetSettings(const RayTracingShadowSettings& settings) { settings_ = settings; }

        /// @brief 現在のシャドウパラメータを取得する
        const RayTracingShadowSettings& GetSettings() const { return settings_; }

    private:
        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex, uint32_t lightIndex);

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        // シェーダーバイトコード
        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;

        // グローバルルートシグネチャ
        GlobalRootSignatureManager globalRootSigMgr_;

        // State Object
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;

        // Shader Table
        ShaderTableBuilder shaderTableBuilder_;

        // ビュー × ライトごとのシャドウ出力テクスチャ [viewIndex][lightIndex]
        struct ShadowView {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            UINT width = 0;
            UINT height = 0;
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            bool dispatchedThisFrame = false;
        };
        ShadowView views_[kViewCount][kMaxDirectionalLights]{};

        // パラメータ
        RayTracingShadowSettings settings_;

        uint32_t frameIndex_        = 0;
        uint32_t dispatchLogCount_  = 0;
        bool isInitialized_         = false;
    };
}
