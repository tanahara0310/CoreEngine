#pragma once
#include "RenderTargetDescriptor.h"
#include "RenderTarget.h"
#include "OffscreenRenderTarget.h"
#include "BackBufferRenderTarget.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>

namespace CoreEngine
{
    class DirectXCommon;

    /// @brief レンダーターゲット管理クラス
    /// レンダーターゲットを名前で管理し、動的な作成・取得を可能にする
    class RenderTargetManager {
    public:
        RenderTargetManager() = default;
        ~RenderTargetManager() = default;

        /// @brief 初期化
        /// @param dxCommon DirectXCommon
        /// @param dsvHeap DSVヒープ
        void Initialize(DirectXCommon* dxCommon, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap);

        // ===== レンダーターゲットの作成 =====

        /// @brief レンダーターゲットを作成
        /// @param desc レンダーターゲット記述子
        /// @return 作成されたレンダーターゲット（失敗時はnullptr）
        RenderTarget* CreateRenderTarget(const RenderTargetDescriptor& desc);

        /// @brief バックバッファターゲットを作成
        /// @param name ターゲット名
        /// @return 作成されたバックバッファターゲット
        RenderTarget* CreateBackBufferTarget(const std::string& name = "BackBuffer");

        // ===== レンダーターゲットの取得 =====

        /// @brief 名前でレンダーターゲットを取得
        /// @param name ターゲット名
        /// @return レンダーターゲット（見つからない場合はnullptr）
        RenderTarget* GetRenderTarget(const std::string& name);

        /// @brief 名前でレンダーターゲットを取得（const版）
        /// @param name ターゲット名
        /// @return レンダーターゲット（見つからない場合はnullptr）
        const RenderTarget* GetRenderTarget(const std::string& name) const;

        /// @brief レンダーターゲットが存在するか確認
        /// @param name ターゲット名
        /// @return 存在する場合true
        bool HasRenderTarget(const std::string& name) const;

        // ===== レンダーターゲットの管理 =====

        /// @brief レンダーターゲットを削除
        /// @param name ターゲット名
        void RemoveRenderTarget(const std::string& name);

        /// @brief すべてのレンダーターゲットをクリア
        void Clear();

        /// @brief 登録されているターゲット名のリストを取得
        /// @return ターゲット名のリスト
        std::vector<std::string> GetRenderTargetNames() const;

        // ===== ウィンドウリサイズ対応 =====

        /// @brief ウィンドウサイズ変更時に自動リサイズ対象のターゲットをリサイズ
        /// @param newWidth 新しい幅
        /// @param newHeight 新しい高さ
        void ResizeAutoTargets(uint32_t newWidth, uint32_t newHeight);

        // ===== デバッグ情報 =====

        /// @brief 登録されているレンダーターゲットの数を取得
        /// @return ターゲット数
        size_t GetRenderTargetCount() const { return targets_.size(); }

        /// @brief DirectXCommonを取得
        /// @return DirectXCommon
        DirectXCommon* GetDirectXCommon() const { return dxCommon_; }

    private:
        // DirectXCommonへの参照
        DirectXCommon* dxCommon_ = nullptr;

        // DSVヒープ
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

        // レンダーターゲットの管理マップ（名前 -> ターゲット）
        std::unordered_map<std::string, std::unique_ptr<RenderTarget>> targets_;

        // レンダーターゲットの記述子マップ（リサイズ時に再利用）
        std::unordered_map<std::string, RenderTargetDescriptor> descriptors_;

        // 次に使用するオフスクリーンインデックス（内部管理用）
        int nextOffscreenIndex_ = 0;

        // 再利用可能なオフスクリーンインデックス
        std::vector<int> freeOffscreenIndices_;
    };
}
