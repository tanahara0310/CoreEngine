#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
    class GraphicsCore;
    class PostEffectManager;

    /// @brief ゲーム映像だけを表示する専用 Win32 ウィンドウ
    /// @details 自前の HWND とスワップチェーンを持ち、ポストエフェクト後の最終出力を転写する。
    ///          ImGui を経由しないので Development ビルドのまま Release と同じ見た目を確認できる。
    /// @warning フレーム内の呼び出し順は ApplyPendingRequests → RecordDrawCommands → Present。
    class GameOutputWindow{
    public:
        GameOutputWindow() = default;
        ~GameOutputWindow();

        GameOutputWindow(const GameOutputWindow&) = delete;
        GameOutputWindow& operator=(const GameOutputWindow&) = delete;

        /// @brief 初期化（ウィンドウはまだ作らない）
        /// @param dxCommon デバイス・コマンドキュー・SRV ヒープの供給元
        /// @param postEffectManager 最終出力テクスチャと転写用エフェクトの供給元
        /// @param mainHwnd エンジン本体のウィンドウ。表示先モニタの決定と、
        ///                 このウィンドウで Esc が押されたときの終了要求先に使う
        void Initialize(GraphicsCore* dxCommon, PostEffectManager* postEffectManager, HWND mainHwnd);

        /// @brief 終了処理（ウィンドウとスワップチェーンを破棄する）
        void Finalize();

        /// @brief 表示状態を要求する（実際の生成・破棄は ApplyPendingRequests で行う）
        /// @param visible 表示するなら true
        void RequestVisible(bool visible) { visibleRequested_ = visible; }

        /// @brief 現在ウィンドウが存在するか
        bool IsOpen() const { return hwnd_ != nullptr; }

        /// @brief ユーザーが × で閉じたか（呼び出し側のトグル状態を同期するために使う）
        /// @return 閉じる操作があったなら true（呼び出すとフラグは落ちる）
        bool ConsumeCloseRequest();

        /// @brief 生成・破棄・リサイズをまとめて適用する
        /// @details GPU 待ちを伴うため、必ずコマンドリストの記録を始める前に呼ぶこと。
        void ApplyPendingRequests();

        /// @brief 最終出力をこのウィンドウのバックバッファへ転写するコマンドを積む
        /// @details メインのコマンドリストへ記録する（同じキューなので順序は保証される）。
        void RecordDrawCommands();

        /// @brief バックバッファを画面へ出す
        /// @details メイン側の ExecuteCommandLists / Present が済んだ後に呼ぶこと。
        void Present();

    private:
        static constexpr UINT kBufferCount = 2;
        static constexpr const wchar_t* kWindowClassName = L"CoreEngineGameOutputWindowClass";

        /// @brief ウィンドウプロシージャ（インスタンスは GWLP_USERDATA から引く）
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        bool Open();
        void Close();

        bool CreateSwapChainResources();
        void ReleaseSwapChainResources();

        /// @brief 保留中のリサイズを適用する（GPU 待ちを含む）
        void ResizeIfRequested();

        GraphicsCore* dxCommon_ = nullptr;
        PostEffectManager* postEffectManager_ = nullptr;

        HWND hwnd_ = nullptr;
        HWND mainHwnd_ = nullptr;
        bool windowClassRegistered_ = false;

        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBufferCount];
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBufferCount] = {};

        /// @brief バックバッファの現在状態（バリア発行の基準）
        /// @details 転写のたびに RENDER_TARGET → PRESENT を往復するため、バッファごとに追跡する。
        D3D12_RESOURCE_STATES backBufferStates_[kBufferCount] = {};

        UINT currentBackBufferIndex_ = 0;
        int32_t clientWidth_ = 0;
        int32_t clientHeight_ = 0;

        bool visibleRequested_ = false;
        bool closeRequested_ = false;
        bool resizeRequested_ = false;
        int32_t requestedWidth_ = 0;
        int32_t requestedHeight_ = 0;

        /// @brief 今フレーム転写コマンドを積んだか（積んでいないフレームで Present しないため）
        bool recordedThisFrame_ = false;
    };
}
