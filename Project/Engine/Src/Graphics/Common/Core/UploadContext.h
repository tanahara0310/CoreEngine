#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace CoreEngine
{
    /// @brief フレーム描画とは独立した GPU 作業用のコマンドコンテキスト
    /// @details 用途は 2 つ。
    ///          1. リソースのアップロード（テクスチャ / 頂点・インデックスバッファ）
    ///          2. フレーム外のオフライン生成（IBL の Irradiance / Prefiltered / BRDF LUT）
    ///
    ///          これらが「フレームの描画用コマンドリスト」を借りていたのが従来の実装で、
    ///          2 つの事故を抱えていた。
    ///          - テクスチャロードはワーカースレッドで走るため、メインスレッドが描画を
    ///            記録している最中の同じコマンドリストへ書き込んでいた
    ///            （ID3D12GraphicsCommandList はスレッドセーフではない）。
    ///          - IBL 生成がフレームのコマンドリストを Close / Execute / Reset していたため、
    ///            記録途中のフレームコマンドを巻き添えで submit する危険があった。
    ///
    ///          本クラスは専用のアロケータ・コマンドリスト・フェンスを持ち、フレームとは
    ///          完全に独立して submit する。キューは共有のため、submit 順 = 実行順であり、
    ///          アップロードは必ずそれを読むフレームより前に実行される。
    ///
    ///          記録は BeginRecording() が返す ScopedRecording が生きている間だけ可能で、
    ///          同時に記録できるのは 1 スレッドのみ（内部ミューテックスで直列化）。
    class UploadContext {
    public:
        /// @brief 記録スコープ（RAII）
        /// @details デストラクタで Close → ExecuteCommandLists → Signal まで行う。
        ///          呼び出し側が Close/Execute を書く必要はない。
        class ScopedRecording {
        public:
            ~ScopedRecording();

            ScopedRecording(ScopedRecording&& other) noexcept;
            ScopedRecording(const ScopedRecording&) = delete;
            ScopedRecording& operator=(const ScopedRecording&) = delete;
            ScopedRecording& operator=(ScopedRecording&&) = delete;

            /// @brief 記録先コマンドリスト（初期化前・失敗時は nullptr）
            ID3D12GraphicsCommandList* List() const { return list_; }

            /// @brief 有効な記録スコープかを返す
            bool IsValid() const { return list_ != nullptr; }

            /// @brief GPU 実行完了まで生存させたいリソースを預ける
            /// @details 主にアップロード用の中間バッファ。フェンス通過後に
            ///          UploadContext::ReleaseCompletedResources() が解放する。
            ///          呼び出し側がメンバとして持ち続ける必要はない。
            void KeepAlive(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

        private:
            friend class UploadContext;
            ScopedRecording(UploadContext* owner, ID3D12GraphicsCommandList* list, size_t batchIndex);

            UploadContext* owner_ = nullptr;
            ID3D12GraphicsCommandList* list_ = nullptr;
            size_t batchIndex_ = 0;
        };

        /// @brief 初期化
        /// @param device D3D12 デバイス
        /// @param queue  submit 先のコマンドキュー（フレームと共有）
        void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

        /// @brief GPU 完了を待ってから全リソースを解放する
        void Shutdown();

        /// @brief 記録を開始する（記録中の他スレッドはここでブロックされる）
        /// @return 記録スコープ。破棄時に自動 submit される
        ScopedRecording BeginRecording();

        /// @brief フェンス通過済みバッチの KeepAlive リソースを解放する
        /// @details フレーム先頭で毎フレーム呼ぶ。呼ばなくても正しく動くが、
        ///          アップロード用中間バッファ（UPLOAD ヒープ＝システムメモリ）が
        ///          解放されず溜まり続ける。
        void ReleaseCompletedResources();

        /// @brief このコンテキストが submit した全作業の GPU 完了を待つ
        /// @details 生成結果をその場で使う処理（IBL 生成など）が使う。
        void WaitForIdle();

        /// @brief 初期化済みかを返す
        bool IsInitialized() const { return device_ != nullptr && queue_ != nullptr; }

    private:
        /// @brief アロケータ 1 本 + そこへ積んだ作業のフェンス値 + 生存させるリソース
        struct Batch {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> keepAlive;
            std::uint64_t fenceValue = 0; ///< 0 = 未 submit（＝再利用可能）
        };

        /// @brief 再利用可能なバッチを 1 つ確保する（無ければ生成、上限なら最古を待つ）
        /// @return batches_ のインデックス（失敗時は SIZE_MAX）
        size_t AcquireBatch();

        /// @brief ScopedRecording の破棄から呼ばれる submit 処理
        void SubmitBatch(size_t batchIndex);

        /// @brief fenceValue の完了を待つ（ロック取得済み前提）
        void WaitForFenceValueLocked(std::uint64_t fenceValue);

        /// @brief 同時記録を 1 スレッドへ直列化する
        std::mutex mutex_;

        ID3D12Device* device_ = nullptr;
        ID3D12CommandQueue* queue_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_; ///< 記録は 1 本ずつなのでリストは 1 本で足りる
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        HANDLE fenceEvent_ = nullptr;
        std::uint64_t nextFenceValue_ = 0;

        std::vector<Batch> batches_;

        /// @brief バッチ数の上限。これを超える場合は最古の完了を待って再利用する
        static constexpr size_t kMaxBatches = 8;
    };
}
