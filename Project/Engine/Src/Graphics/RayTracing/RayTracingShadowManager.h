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
    ///          GameView / ReflectionView など View ごとに独立した結果を保持できるようにする
    /// @brief DXRシャドウのパラメータ設定
    struct RayTracingShadowSettings {
        float shadowBias = 0.05f;          ///< セルフシャドウ防止バイアス
        float maxRayDistance = 1000.0f;    ///< シャドウレイの最大射程距離
        float lightRadius = 0.02f;         ///< 光源の角半径（ラジアン）
        ///< 実際の太陽: ~0.0046 rad
        ///< 0.02 = わずかにソフトな影（ペナンブラが狭くVariance Clampingが効く）
        ///< 0.15 はペナンブラが広すぎてゴーストが発生するため禁止
        int   softShadowSamples = 1;       ///< ソフトシャドウのサンプル数（A-Trousデノイザーで補完するため1で十分）
        ///< A-Trous 3パスデノイザー適用済みのため 1 で十分な品質が得られる
        ///< 高品質なソフトシャドウが必要な場合は 2〜4 程度まで増やす（GPUコストはサンプル数に比例）
        float historyAlpha = 0.15f;        ///< テンポラル蓄積ブレンド係数
        ///< Variance Clampingと組み合わせて使用する固定値
    };

    class RayTracingShadowManager {
    public:
        /// @brief ビュー識別子
        enum class ViewID : uint32_t {
            GameView = 0,
            ReflectionView = 1,
            Count
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
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
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
            const Vector3& lightDirection,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief A-Trous デノイズパスを実行する（Dispatch の直後に呼ぶ）
        void Denoise(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 空間前処理＋テンポラル蓄積パスを実行する（Dispatch と Denoise の間に呼ぶ）
        void ApplyTemporal(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャを取得する
        /// @param viewId 参照するビュー ID
        /// @param lightIndex 参照するディレクショナルライト番号
        /// @return シャドウ結果テクスチャ。未確保なら nullptr
        ID3D12Resource* GetShadowResource(
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 指定ビュー・ライトのシャドウ結果リソース状態参照を取得する
        /// @param viewId 参照するビュー ID
        /// @param lightIndex 参照するディレクショナルライト番号
        /// @return 自動遷移処理が共有する状態変数への参照
        D3D12_RESOURCE_STATES& GetShadowCurrentState(
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

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

            // テンポラル蓄積用履歴テクスチャ（前フレームの蓄積結果を保持）
            Microsoft::WRL::ComPtr<ID3D12Resource> historyTexture;
            D3D12_GPU_DESCRIPTOR_HANDLE historySrvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE historySrvCpuHandle{};
            D3D12_RESOURCE_STATES historyCurrentState = D3D12_RESOURCE_STATE_COMMON;
            bool isHistoryValid = false; ///< 履歴テクスチャが初回フレーム書き込み済みか

            // A-Trous デノイズ用中間バッファ（ping-pong）
            Microsoft::WRL::ComPtr<ID3D12Resource> denoiseTemp;
            D3D12_GPU_DESCRIPTOR_HANDLE denoiseTempUavHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE denoiseTempUavCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE denoiseTempSrvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE denoiseTempSrvCpuHandle{};
            D3D12_RESOURCE_STATES denoiseTempState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };
        ShadowView views_[kViewCount][kMaxDirectionalLights]{};

        // パラメータ
        RayTracingShadowSettings settings_;

        uint32_t frameIndex_ = 0;
        uint32_t dispatchLogCount_ = 0;
        bool isInitialized_ = false;

        // A-Trous デノイズ用コンピュートパイプライン
        Microsoft::WRL::ComPtr<ID3D12RootSignature> denoiseRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> denoisePipelineState_;
        bool denoiseInitialized_ = false;

        // テンポラル蓄積用コンピュートパイプライン
        Microsoft::WRL::ComPtr<ID3D12RootSignature> temporalRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> temporalPipelineState_;
        bool temporalInitialized_ = false;
    };
}
