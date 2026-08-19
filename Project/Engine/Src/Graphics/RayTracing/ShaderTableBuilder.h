#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>

namespace CoreEngine
{
    /// @brief DXR シェーダーテーブルビルダー
    /// @details RayGen / Miss / HitGroup を登録して Build() で GPU バッファを生成し、
    ///          以降は BuildDispatchDesc() で DispatchRays 記述子を得る。
    class ShaderTableBuilder {
    public:
        /// @brief RayGen シェーダーを設定する（1 パスにつき 1 つ）
        ShaderTableBuilder& SetRayGenShader(const wchar_t* name);

        /// @brief Miss シェーダーを追加する（複数可、追加順が Miss インデックスになる）
        ShaderTableBuilder& AddMissShader(const wchar_t* name);

        /// @brief HitGroup を追加する（複数可、追加順が HitGroup インデックスになる）
        ShaderTableBuilder& AddHitGroup(const wchar_t* name);

        /// @brief シェーダーテーブルバッファを構築する
        /// @param device  D3D12 デバイス
        /// @param props   State Object Properties（GetShaderIdentifier の取得元）
        /// @return 成功した場合 true
        bool Build(ID3D12Device* device, ID3D12StateObjectProperties* props);

        /// @brief DispatchRays 記述子を生成する（Build() 後に呼び出す）
        /// @param width   ディスパッチ幅（ピクセル数）
        /// @param height  ディスパッチ高さ（ピクセル数）
        /// @param depth   奥行き（通常 1）
        D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(UINT width, UINT height, UINT depth = 1) const;

        /// @brief 構築済みか
        bool IsBuilt() const { return buffer_ != nullptr; }

    private:
        static UINT64 Align(UINT64 size, UINT64 alignment);

        const wchar_t* rayGenName_ = nullptr;
        std::vector<const wchar_t*> missNames_;
        std::vector<const wchar_t*> hitGroupNames_;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;

        UINT64 recordSize_ = 0;  ///< 1レコードのバイト数（アライメント済み）
        UINT64 rayGenOffset_ = 0;  ///< RayGen レコードのバッファ内オフセット
        UINT64 missOffset_ = 0;  ///< Miss テーブル先頭のバッファ内オフセット
        UINT64 missTableSize_ = 0;  ///< Miss テーブルの実データサイズ（DispatchRays 用）
        UINT64 hitGroupOffset_ = 0;  ///< HitGroup テーブル先頭のバッファ内オフセット
        UINT64 hitGroupTableSize_ = 0;  ///< HitGroup テーブルの実データサイズ（DispatchRays 用）
    };
}
