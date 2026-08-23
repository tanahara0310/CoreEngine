#pragma once
#include "RenderTargetNames.h"
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
    class GraphicsCore;
    class SceneDepth;

    /// @brief レンダーターゲット管理クラス
    /// レンダーターゲットを名前で管理し、動的な作成・取得を可能にする
    class RenderTargetManager {
    public:
        RenderTargetManager() = default;
        ~RenderTargetManager();

        /// @brief 初期化
        /// @param dxCommon GraphicsCore
        /// @param sharedDepth オフスクリーンターゲットが共有するシーン深度（DSV の供給元）
        void Initialize(GraphicsCore* dxCommon, SceneDepth* sharedDepth);

        // ===== レンダーターゲットの作成 =====

        /// @brief レンダーターゲットを作成
        /// @param desc レンダーターゲット記述子
        /// @return 作成されたレンダーターゲット（失敗時はnullptr）
        RenderTarget* CreateRenderTarget(const RenderTargetDescriptor& desc);

        /// @brief バックバッファターゲットを作成
        /// @param name ターゲット名
        /// @return 作成されたバックバッファターゲット
        RenderTarget* CreateBackBufferTarget(const std::string& name);

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

        /// @brief GraphicsCoreを取得
        /// @return GraphicsCore
        GraphicsCore* GetGraphicsCore() const { return dxCommon_; }

        /// @brief PostEffect intermediate 用ターゲット名を取得する
        /// @param index intermediate インデックス
        /// @return 物理ターゲット名
        static std::string MakePostEffectIntermediateTargetName(size_t index);

        /// @brief PostEffect チェーンの中間ターゲット枚数
        /// @details エフェクトは「直前の出力」しか読まないので、交互に使う 2 枚で足りる。
        ///          以前は「有効エフェクト数 - 1」枚を確保しており、17 個全部有効にすると
        ///          フル解像度 HDR が 16 枚（1080p で約 400MB）並んでいた。
        static constexpr size_t kPostEffectPingPongCount = 2;

        /// @brief PostEffect intermediate 用ターゲットを必要数ぶん確保する
        /// @param count 必要な intermediate 数
        void EnsurePostEffectIntermediateTargets(size_t count);

        /// @brief 確保済みレンダーターゲットの合計バイト数を返す（付随する深度も含む）
        size_t CalcTotalAllocatedBytes() const;

        /// @brief 合計サイズが前回と変わったときだけログへ出す
        /// @details 毎フレーム呼んでよい。増減したときだけ 1 行出るので、
        ///          チェーン構成やウィンドウサイズを変えたときの使用量が追える。
        void LogAllocationIfChanged();

        /// @brief PostEffect intermediate 用ターゲットを取得する
        /// @param index intermediate インデックス
        /// @return 対応するレンダーターゲット。無い場合は nullptr
        RenderTarget* GetPostEffectIntermediateTarget(size_t index);

        /// @brief PostEffect final 用ターゲットを確保する
        void EnsurePostEffectFinalTarget();

        /// @brief PostEffect final 用ターゲットを取得する
        /// @return 対応するレンダーターゲット。無い場合は nullptr
        RenderTarget* GetPostEffectFinalTarget();

    private:
        /// @brief 直近にログへ出した合計バイト数（変化検出用）
        size_t lastLoggedBytes_ = 0;

        // GraphicsCoreへの参照
        GraphicsCore* dxCommon_ = nullptr;

        // オフスクリーンターゲットが共有するシーン深度（非所有。所有者は RenderDomainContext）
        SceneDepth* sharedDepth_ = nullptr;

        // レンダーターゲットの管理マップ（名前 -> ターゲット）
        std::unordered_map<std::string, std::unique_ptr<RenderTarget>> targets_;

        // レンダーターゲットの記述子マップ（リサイズ時に再利用）
        std::unordered_map<std::string, RenderTargetDescriptor> descriptors_;

        // 次に使用するオフスクリーンインデックス（内部識別用）
        int nextOffscreenIndex_ = 0;

        // 再利用可能なオフスクリーンインデックス
        std::vector<int> freeOffscreenIndices_;
    };
}
