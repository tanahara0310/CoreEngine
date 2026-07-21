#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief 平面反射ビューの解像度スケール
    /// @details 反射はシーン全体をもう一周描画するため、フル解像度だと G-Buffer ラスタライズが
    ///          メインビューと同等以上のコストになる。水面の歪みで低解像度の劣化はほぼ視認できないため
    ///          1/2 解像度（ピクセル仕事 1/4）で描画する。反射用 G-Buffer / 深度 / 出力ターゲットの
    ///          3 点が必ず同一スケールであること（DeferredLighting が Load(ピクセル座標) で読むため）。
    inline constexpr float kReflectionViewResolutionScale = 0.5f;

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
        /// @details RenderGraph の SceneColor / PostEffect / DeferredLighting 系は HDR 前提のため既定は R16G16B16A16_FLOAT とする
        DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;

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

        /// @brief ウィンドウ追従時の解像度スケール（width / height が 0 のときのみ適用）
        /// @details 0.5 で 1/2 解像度。反射など低解像度で十分なビューのターゲットに使う。
        float resolutionScale = 1.0f;

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
