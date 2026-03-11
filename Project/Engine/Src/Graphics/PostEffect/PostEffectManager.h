#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <d3d12.h>
#include <cassert>

#include "Graphics/PostEffect/PostEffectBase.h"
#include "Graphics/PostEffect/PostEffectNames.h"
#include "PostEffectPresetManager.h"

namespace CoreEngine
{
// 前方宣言
class DirectXCommon;
class Render;
class RenderTarget;

/// @brief ポストエフェクト管理クラス
class PostEffectManager {
public:
    /// @brief 初期化
    /// @param dxCommon DirectXCommonのポインタ
    /// @param render Renderクラスのポインタ
    void Initialize(DirectXCommon* dxCommon, Render* render);

    /// @brief テンプレートでエフェクトを登録（型推論による簡潔な登録）
    /// @tparam T エフェクトの型（PostEffectBaseを継承）
    /// @param name エフェクト名
    /// @param enabled 初期有効状態
    template<typename T>
    void RegisterEffect(const std::string& name, bool enabled = false);

    /// @brief 型安全なエフェクト取得
    /// @tparam T エフェクトの型
    /// @param name エフェクト名
    /// @return キャストされたエフェクトのポインタ（見つからない場合はnullptr）
    template<typename T>
    T* GetEffect(const std::string& name);

    /// @brief 型安全なエフェクト取得（const版）
    /// @tparam T エフェクトの型
    /// @param name エフェクト名
    /// @return キャストされたエフェクトのポインタ（見つからない場合はnullptr）
    template<typename T>
    const T* GetEffect(const std::string& name) const;

    /// @brief 特定のポストエフェクトを実行（有効/無効チェックなし）
    /// @param name エフェクト名
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    void ExecuteEffect(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

    /// @brief エフェクトの有効/無効を設定
    /// @param effectName エフェクト名
    /// @param enabled 有効にするかどうか
    void SetEffectEnabled(const std::string& effectName, bool enabled);

    /// @brief エフェクトが有効かどうかを取得
    /// @param effectName エフェクト名
    /// @return 有効ならtrue
    bool IsEffectEnabled(const std::string& effectName) const;

    /// @brief エフェクトチェーンの順序を設定
    /// @param effectNames エフェクト名のリスト
    void SetEffectChain(const std::vector<std::string>& effectNames);

    /// @brief 現在のエフェクトチェーンを取得
    /// @return エフェクト名のリスト
    const std::vector<std::string>& GetEffectChain() const;

    /// @brief 更新処理
    /// @param deltaTime フレーム時間
    void Update(float deltaTime);

    /// @brief ImGuiでポストエフェクトのパラメータを調整
    void DrawImGui();

    /// @brief プリセットマネージャーを取得
    /// @return プリセットマネージャーの参照
    PostEffectPresetManager& GetPresetManager() { return *presetManager_; }

    /// @brief 現在表示すべき最終テクスチャハンドルを取得
    /// @return 表示すべきテクスチャのSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetFinalDisplayTextureHandle() const;

    /// @brief エフェクトチェーンを実行し、結果のテクスチャハンドルを取得
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    /// @return 最終出力のSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE ExecuteEffectChain(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

    /// @brief Bloomエフェクトをマスク付きで実行（テスト用）
    /// @param inputSrvHandle 入力テクスチャのSRVハンドル
    /// @param maskSrvHandle マスクテクスチャのSRVハンドル
    /// @return 最終出力のSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE ExecuteBloomWithMask(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE maskSrvHandle);

private:
    /// @brief Ping-Pongバッファ管理用ヘルパークラス
    class PingPongBuffer {
    public:
        PingPongBuffer(DirectXCommon* dxCommon, Render* render);

        /// @brief エフェクトを適用し、バッファを切り替える
        /// @param effect 適用するエフェクト
        /// @return 適用されたかどうか
        bool ApplyEffect(PostEffectBase* effect);

        /// @brief 現在の出力SRVハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentOutput() const;

        /// @brief 最終結果をオフスクリーン#1に保証する
        /// @param fullScreenEffect FullScreenエフェクト（コピー用）
        void EnsureOutputInBuffer1(PostEffectBase* fullScreenEffect);

        /// @brief 入力をリセット
        void Reset(D3D12_GPU_DESCRIPTOR_HANDLE input);

    private:
        DirectXCommon* dxCommon_;
        Render* render_;
        D3D12_GPU_DESCRIPTOR_HANDLE currentInput_;
        int currentOutputIndex_;

        RenderTarget* GetRenderTarget(int index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandle(int index) const;
    };

    /// @brief 全エフェクトを登録
    void RegisterAllEffects();

    /// @brief ポストエフェクトを登録（内部用）
    /// @param name エフェクト名
    /// @param effect ポストエフェクトのインスタンス
    void RegisterEffectInternal(const std::string& name, std::unique_ptr<PostEffectBase> effect);

    /// @brief エフェクト取得（内部用・型なし）
    /// @param name エフェクト名
    /// @return ポストエフェクトのポインタ（見つからない場合はnullptr）
    PostEffectBase* GetEffectInternal(const std::string& name);

    /// @brief エフェクト取得（内部用・型なし・const版）
    /// @param name エフェクト名
    /// @return ポストエフェクトのポインタ（見つからない場合はnullptr）
    const PostEffectBase* GetEffectInternal(const std::string& name) const;

    /// @brief 有効なエフェクトの名前リストを収集
    /// @param effectNames エフェクト名のリスト
    /// @return 有効なエフェクト名のリスト
    std::vector<std::string> CollectEnabledEffectNames(const std::vector<std::string>& effectNames) const;

    DirectXCommon* directXCommon_ = nullptr;
    Render* render_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<PostEffectBase>> effects_;
    
    std::vector<std::string> effectChain_ = { 
        PostEffectNames::FadeEffect, 
        PostEffectNames::Shockwave, 
        PostEffectNames::Blur, 
        PostEffectNames::RadialBlur, 
        PostEffectNames::RasterScroll, 
        PostEffectNames::ColorGrading, 
        PostEffectNames::ChromaticAberration, 
        PostEffectNames::Sepia, 
        PostEffectNames::Invert, 
        PostEffectNames::GrayScale, 
        PostEffectNames::Vignette,
        PostEffectNames::Bloom,
        PostEffectNames::Dissolve
    };
    
    std::unique_ptr<PostEffectPresetManager> presetManager_;
    
    D3D12_GPU_DESCRIPTOR_HANDLE finalDisplayHandle_;
};

// =============================================================================
// テンプレート関数の実装
// =============================================================================

template<typename T>
void PostEffectManager::RegisterEffect(const std::string& name, bool enabled)
{
    static_assert(std::is_base_of<PostEffectBase, T>::value, 
        "T must inherit from PostEffectBase");
    
    auto effect = std::make_unique<T>();
    effect->Initialize(directXCommon_);
    effect->SetEnabled(enabled);
    RegisterEffectInternal(name, std::move(effect));
}

template<typename T>
T* PostEffectManager::GetEffect(const std::string& name)
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return dynamic_cast<T*>(it->second.get());
    }
    return nullptr;
}

template<typename T>
const T* PostEffectManager::GetEffect(const std::string& name) const
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return dynamic_cast<const T*>(it->second.get());
    }
    return nullptr;
}
}
