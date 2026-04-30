#pragma once
#include <d3d12.h>
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    /// @brief IBL（Image-Based Lighting）関連パラメータ
    /// @note BaseModelRenderer へのセッター群を構造体化して設定漏れを防止
    struct IBLParameters {
        D3D12_GPU_DESCRIPTOR_HANDLE environmentMap{};  ///< 環境マップ（スペキュラー反射用）
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceMap{};   ///< 拡散IBL用キューブマップ
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMap{};  ///< スペキュラーIBL用プリフィルタマップ
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUT{};         ///< BRDF積分テーブル

        Vector3 rotation{};                            ///< 環境マップ回転角度（ラジアン）
        float intensity = 1.0f;                        ///< 環境輝度スケール

        /// @brief すべてのテクスチャが設定済みか確認
        /// @return 全テクスチャが有効な場合 true
        bool IsFullyConfigured() const {
            return environmentMap.ptr != 0
                && irradianceMap.ptr != 0
                && prefilteredMap.ptr != 0
                && brdfLUT.ptr != 0;
        }

        /// @brief 環境マップのみが設定済みか確認
        /// @return 環境マップが有効な場合 true
        bool HasEnvironmentMap() const {
            return environmentMap.ptr != 0;
        }

        /// @brief パラメータをリセット
        void Reset() {
            environmentMap = {};
            irradianceMap = {};
            prefilteredMap = {};
            brdfLUT = {};
            rotation = {};
            intensity = 1.0f;
        }
    };
}
