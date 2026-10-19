#pragma once

#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/Vector/Vector4.h"

#include <d3d12.h>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
    class ShaderMaterialPipeline;
    class UploadRing;
    struct MaterialParamLayout;

    /// @brief 描画の部品が持つマテリアルの欄（共有のマテリアルと、この部品だけの上書き）
    /// @details 上書きは Unity の MaterialPropertyBlock に当たり、保存しない（スクリプトが再生中に決める）。
    ///          値は「上書き → マテリアル → 0」の順に引く。
    class ShaderMaterialSlot
    {
    public:
        // ===== 指す先 =====

        /// @brief 指しているマテリアル（型記述子とやり取りする値）
        Reflection::AssetRefValue GetAsset() const { return asset_.GetValue(); }

        /// @brief マテリアルを指し直す（読み込みは Resolve で行う）
        /// @return 指す先が変わったら true
        bool SetAsset(const Reflection::AssetRefValue& value);

        /// @brief マテリアルを指しているか
        bool IsSet() const { return asset_.IsSet(); }

        /// @brief 指している .mat のパス（指していなければ空）
        std::string GetPath() const { return asset_.IsSet() ? asset_.GetPath() : std::string(); }

        /// @brief 指しているマテリアルを読み込む（読めなければ空のまま）
        void Resolve();

        /// @brief 読み込んだ共有のマテリアル（指していない・読めなければ nullptr）
        ShaderMaterial* GetMaterial() const { return material_.get(); }

        // ===== この部品だけの上書き =====

        /// @brief 値を上書きする
        void SetNumbers(std::string_view name, std::span<const float> numbers);

        /// @brief テクスチャを上書きする（空のパスなら上書きをやめる）
        void SetTexture(std::string_view name, const std::string& path);

        /// @brief 上書きを全部やめる
        void ClearOverrides();

        /// @brief 値を引く（上書き → マテリアルの順。無ければ nullptr）
        const MaterialNumbers* FindNumbers(std::string_view name) const;

        // ===== 描画 =====

        /// @brief パイプラインのルート引数を差す（ルートシグネチャとパイプラインは差してある前提）
        /// @param transform 板の変換の定数（`gSpriteTransform`）
        /// @param draw エンジンが渡す値の定数（`gSpriteDraw`）
        /// @param mainTexture 部品のテクスチャ（`gTexture`）
        /// @return 描けるなら true
        bool Bind(ID3D12GraphicsCommandList* commandList, const ShaderMaterialPipeline& pipeline, UploadRing& uploadRing,
            D3D12_GPU_VIRTUAL_ADDRESS transform, D3D12_GPU_VIRTUAL_ADDRESS draw,
            D3D12_GPU_DESCRIPTOR_HANDLE mainTexture);

    private:
        /// @brief 値を定数バッファの並びに詰め直す（並びか値が変わったときだけ）
        void Pack(const MaterialParamLayout& layout);

        /// @brief テクスチャを指していないときに差す白
        D3D12_GPU_DESCRIPTOR_HANDLE GetWhiteTexture();

        AssetRef<ShaderMaterialAsset> asset_;
        std::shared_ptr<ShaderMaterial> material_;

        std::map<std::string, MaterialNumbers, std::less<>> overrideValues_;
        std::map<std::string, TextureManager::LoadedTexture, std::less<>> overrideTextures_;
        uint64_t overridesRevision_ = 1;

        const MaterialParamLayout* packedLayout_ = nullptr;
        uint64_t packedMaterialRevision_ = 0;
        uint64_t packedOverridesRevision_ = 0;
        const ShaderMaterial* packedMaterial_ = nullptr;
        std::vector<uint8_t> packed_;

        TextureManager::LoadedTexture whiteTexture_{};
    };
}
