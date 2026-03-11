#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief レンダーターゲットの記述子
    /// レンダーターゲットの作成に必要な情報を保持
    struct RenderTargetDescriptor {
        /// @brief レンダーターゲットの名前（識別用）
        std::string name;

        /// @brief 幅（0の場合はウィンドウサイズに自動追従）
        uint32_t width = 0;

        /// @brief 高さ（0の場合はウィンドウサイズに自動追従）
        uint32_t height = 0;

        /// @brief カラーフォーマット
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

        /// @brief 深度ステンシルバッファが必要か
        bool needsDepthStencil = true;

        /// @brief 深度ステンシルフォーマット
        DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;

        /// @brief SRV（Shader Resource View）が必要か（テクスチャとして読む場合）
        bool needsSRV = true;

        /// @brief クリアカラー（RGBA）
        float clearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

        /// @brief ウィンドウリサイズ時に自動的にリサイズするか
        bool autoResize = true;

        /// @brief デフォルトコンストラクタ
        RenderTargetDescriptor() = default;

        /// @brief 名前付きコンストラクタ
        /// @param targetName ターゲット名
        explicit RenderTargetDescriptor(const std::string& targetName)
            : name(targetName) {}

        /// @brief 簡易コンストラクタ
        /// @param targetName ターゲット名
        /// @param w 幅
        /// @param h 高さ
        RenderTargetDescriptor(const std::string& targetName, uint32_t w, uint32_t h)
            : name(targetName), width(w), height(h) {}
    };
}
