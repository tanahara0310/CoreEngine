#pragma once
#include "RenderingTechniqueBase.h"
#include <memory>
#include <unordered_map>
#include <string>

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief レンダリング技術管理クラス
    /// @details SSAO、TAA、SSRなどの高度なレンダリング技術を管理
    ///          PostEffectManager とは独立した管理を行う
    class RenderingTechniqueManager {
    public:
        RenderingTechniqueManager() = default;
        ~RenderingTechniqueManager() = default;

        /// @brief 初期化
        /// @param dxCommon GraphicsCore
        void Initialize(GraphicsCore* dxCommon, ShaderProgramCache* shaderProgramCache);

        /// @brief レンダリング技術を登録
        /// @tparam T RenderingTechniqueBase を継承した型
        /// @param name 技術の名前
        /// @param enabled 初期状態で有効かどうか
        template<typename T>
        void RegisterTechnique(const std::string& name, bool enabled = true);

        /// @brief レンダリング技術を取得
        /// @tparam T 取得する技術の型
        /// @param name 技術の名前
        /// @return 技術のポインタ（存在しない場合nullptr）
        template<typename T>
        T* GetTechnique(const std::string& name);

        /// @brief レンダリング技術を取得（const版）
        /// @tparam T 取得する技術の型
        /// @param name 技術の名前
        /// @return 技術のポインタ（存在しない場合nullptr）
        template<typename T>
        const T* GetTechnique(const std::string& name) const;

        /// @brief レンダリング技術が存在するか確認
        /// @param name 技術の名前
        /// @return 存在する場合true
        bool HasTechnique(const std::string& name) const;

        /// @brief 全技術の更新処理
        /// @param deltaTime フレーム時間
        void Update(float deltaTime);

        /// @brief リサイズ時の処理
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void OnResize(uint32_t width, uint32_t height);

        /// @brief ImGuiでデバッグ表示
        void DrawImGui();

    private:
        GraphicsCore* graphicsCore_ = nullptr;
        /// @brief 各技術へ配るシェーダーキャッシュ（所有者はエンジン）
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        std::unordered_map<std::string, std::unique_ptr<RenderingTechniqueBase>> techniques_;

        /// @brief 全技術を登録（内部で各技術をRegisterする）
        void RegisterAllTechniques();
    };

    // ========================================
    // テンプレート実装
    // ========================================

    template<typename T>
    void RenderingTechniqueManager::RegisterTechnique(const std::string& name, bool enabled)
    {
        static_assert(std::is_base_of<RenderingTechniqueBase, T>::value,
            "T must inherit from RenderingTechniqueBase");

        auto technique = std::make_unique<T>();
        // 派生クラスの Initialize シグネチャを変えずにキャッシュを渡すための注入点
        technique->SetShaderProgramCache(shaderProgramCache_);
        technique->Initialize(graphicsCore_);
        technique->SetEnabled(enabled);
        techniques_[name] = std::move(technique);
    }

    template<typename T>
    T* RenderingTechniqueManager::GetTechnique(const std::string& name)
    {
        auto it = techniques_.find(name);
        if (it != techniques_.end()) {
            return dynamic_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    template<typename T>
    const T* RenderingTechniqueManager::GetTechnique(const std::string& name) const
    {
        auto it = techniques_.find(name);
        if (it != techniques_.end()) {
            return dynamic_cast<const T*>(it->second.get());
        }
        return nullptr;
    }
}
