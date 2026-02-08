#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>

#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "MathCore.h"
#include "Structs/MaterialConstants.h"

/// @brief マテリアル管理クラス
/// @details GPU定数バッファの管理を行います。
namespace CoreEngine
{
    class MaterialManager {
    public: // メンバ関数

        /// @brief 初期化
        /// @param device デバイス
        /// @param resourceFactory リソース生成
        void Initialize(ID3D12Device* device, ResourceFactory* resourceFactory);

        // ===== 直接アクセスAPI =====

        /// @brief マテリアル定数データへの直接アクセスを取得
        /// @return マテリアル定数データへのポインタ
        MaterialConstants* GetConstants()
        {
            return materialData_;
        }

        /// @brief マテリアル定数データへの直接アクセスを取得（const版）
        /// @return マテリアル定数データへのポインタ
        const MaterialConstants* GetConstants() const
        {
            return materialData_;
        }

        /// @brief マテリアルのGPU仮想アドレスを取得
        /// @return GPU仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
        {
            return materialResource_->GetGPUVirtualAddress();
        }

    private: // メンバ変数
        // マテリアルリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
        // マテリアルデータ
        MaterialConstants* materialData_ = nullptr;
    };
}
