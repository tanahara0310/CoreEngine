#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/// @file
/// @brief PSO をディスクに保存して、次の起動から使い回すキャッシュ

namespace CoreEngine
{
    /// @brief ルートシグネチャに付ける、中身（シリアライズした blob）のハッシュの印
    /// @details PipelineCache が PSO の名前を決めるときに読む。ルートシグネチャを作るところで付ける。
    // {6C3F2A41-8D1E-4B7A-9C55-2E7D4F1A0B93}
    inline constexpr GUID kRootSignatureHashGuid =
        { 0x6c3f2a41, 0x8d1e, 0x4b7a, { 0x9c, 0x55, 0x2e, 0x7d, 0x4f, 0x1a, 0x0b, 0x93 } };

    /// @brief PSO をディスクに保存し、次の起動からドライバのコンパイルを省くキャッシュ（ID3D12PipelineLibrary）
    /// @details PSO の中身（シェーダ・ステート・ルートシグネチャ）のハッシュを名前にして引く。
    ///          無ければ作って足し、Save でファイルへ書く。ドライバが変わって読めないファイルは捨てて作り直す。
    ///          どのスレッドから作ってもよい。
    class PipelineCache {
    public:
        static PipelineCache& Get();

        /// @brief キャッシュのファイルを読んで準備する（無い・読めないときは空から始める）
        /// @param device D3D12 デバイス
        /// @param file キャッシュのファイル
        void Initialize(ID3D12Device* device, const std::filesystem::path& file);

        /// @brief 足したものがあればファイルへ書き、ライブラリを手放す
        void Shutdown();

        /// @brief 足したものがあればファイルへ書く
        /// @return 書いたら true
        bool Save();

        /// @brief ここまでに引いた数・作った数と、それぞれにかかった時間をログへ出す
        void LogStats();

        /// @brief コンピュートの PSO をキャッシュから引く（無ければ作って足す）
        /// @param outResult 作ったときの HRESULT（引けたら S_OK）
        Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCompute(
            ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, HRESULT* outResult = nullptr);

        /// @brief グラフィックスの PSO をキャッシュから引く（無ければ作って足す）
        /// @param outResult 作ったときの HRESULT（引けたら S_OK）
        Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphics(
            ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, HRESULT* outResult = nullptr);

        /// @brief ルートシグネチャに中身のハッシュの印を付ける
        /// @param rootSignature 付ける先
        /// @param blob ルートシグネチャのシリアライズした中身
        /// @param size blob のバイト数
        static void TagRootSignature(ID3D12RootSignature* rootSignature, const void* blob, size_t size);

    private:
        PipelineCache() = default;

        /// @brief library_ を引き、無ければ作って足す共通処理
        template <class Desc, class LoadFn, class CreateFn>
        Microsoft::WRL::ComPtr<ID3D12PipelineState> LoadOrCreate(
            const std::wstring& name, const Desc& desc, LoadFn&& load, CreateFn&& create, HRESULT* outResult);

        /// @brief 空のライブラリを作る（mutex_ を持った状態で呼ぶ）
        bool CreateEmptyLibrary();

        /// @brief ライブラリが参照する、ファイルから読んだ中身（ライブラリより後に破棄する）
        std::vector<uint8_t> blob_;
        Microsoft::WRL::ComPtr<ID3D12Device1> device_;
        Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> library_;
        std::filesystem::path file_;

        std::mutex mutex_;
        /// @brief この起動で足したものがある
        bool dirty_ = false;
        /// @brief 同じ名前で中身の違うものがあった（保存のときに、この起動で使ったものだけで作り直す）
        bool stale_ = false;
        /// @brief この起動で引いたか作った PSO（作り直すときに入れ直す）
        std::vector<std::pair<std::wstring, Microsoft::WRL::ComPtr<ID3D12PipelineState>>> used_;
        uint32_t hits_ = 0;
        uint32_t misses_ = 0;
        double hitSeconds_ = 0.0;
        double missSeconds_ = 0.0;
    };
}
