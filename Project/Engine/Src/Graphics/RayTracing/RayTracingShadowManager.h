#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    /// @brief DXR レイトレーシングシャドウを管理するクラス
    /// @details State Object / Shader Table / UAV テクスチャの作成と DispatchRays を担当
    ///          SceneView / GameView で独立した結果を保持するため、2枚のシャドウテクスチャを持つ
    class RayTracingShadowManager {
    public:
        /// @brief ビュー識別子
        enum class ViewID : uint32_t {
            SceneView = 0,
            GameView = 1,
            Count
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);

        /// @brief 初期化（State Object / Shader Table / UAV テクスチャの構築）
        /// @return 成功した場合 true
        bool Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        /// @brief シャドウレイをディスパッチする
        void Dispatch(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const Vector3& lightDirection,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView);

        /// @brief 指定ビューのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView) const;

        /// @brief 初期化済みか
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 指定ビューで今フレームにディスパッチ済みか
        bool IsDispatchedThisFrame(ViewID viewId = ViewID::GameView) const;

        /// @brief フレーム開始時に全ビューの状態をリセット
        void ResetFrameState();

        /// @brief シャドウバイアスを設定
        void SetShadowBias(float bias) { shadowBias_ = bias; }

        /// @brief レイの最大距離を設定
        void SetMaxRayDistance(float dist) { maxRayDistance_ = dist; }

    private:
        bool CompileShader();
        bool CreateRootSignature();
        bool CreateStateObject();
        bool CreateShaderTable();
        bool CreateOutputTexture(UINT width, UINT height, uint32_t viewIndex);

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        // シェーダーバイトコード（ID3DBlob として保持、dxcapi.h のヘッダ依存を避ける）
        Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob_;

        // ルートシグネチャ（グローバル）
        Microsoft::WRL::ComPtr<ID3D12RootSignature> globalRootSignature_;

        // State Object
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;

        // Shader Table
        Microsoft::WRL::ComPtr<ID3D12Resource> shaderTable_;
        UINT64 shaderTableRecordSize_ = 0;
        UINT64 shaderTableSize_ = 0;
        UINT64 rayGenOffset_ = 0;
        UINT64 missOffset_ = 0;
        UINT64 hitGroupOffset_ = 0;
        UINT64 sectionSize_ = 0;

        // ビューごとのシャドウ出力テクスチャ（ダブルバッファ）
        struct ShadowView {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            UINT width = 0;
            UINT height = 0;
            bool needsTransitionToUAV = false;
            bool dispatchedThisFrame = false;
        };
        ShadowView views_[kViewCount]{};

        // 定数バッファ
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;

        // パラメータ
        float shadowBias_ = 0.05f;
        float maxRayDistance_ = 1000.0f;

        bool isInitialized_ = false;
    };
}
