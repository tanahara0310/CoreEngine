#pragma once
#include <Math/MathCore.h>
#include <d3d12.h>

/// @brief カメラのタイプ

namespace CoreEngine
{

    // 前方宣言
    struct CameraParameters;

    enum class CameraType {
        Camera3D,  // 3D用カメラ（透視投影）
        Camera2D   // 2D用カメラ（正射影）
    };

    /// @brief カメラインターフェース
    class ICamera {
    public:

        virtual ~ICamera() = default;

        /// @brief カメラの更新
        virtual void Update() = 0;

        /// @brief ビューマトリックスの取得
        virtual const Matrix4x4& GetViewMatrix() const = 0;

        /// @brief プロジェクションマトリックスの取得
        virtual const Matrix4x4& GetProjectionMatrix() const = 0;

        /// @brief カメラの位置取得
        virtual Vector3 GetPosition() const = 0;

        /// @brief カメラのGPU仮想アドレスを取得
        /// @return カメラ用定数バッファのGPU仮想アドレス
        virtual D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const = 0;

        /// @brief カメラの行列をGPUに転送
        virtual void TransferMatrix() = 0;

        /// @brief カメラの有効/無効状態を設定
        virtual void SetActive(bool isActive) { this->isActive_ = isActive; }

        /// @brief カメラの有効/無効状態を取得
        virtual bool GetActive() const { return isActive_; }

        /// @brief カメラのタイプを取得
        virtual CameraType GetCameraType() const = 0;

        /// @brief カメラパラメータを取得（デフォルト実装）
        virtual CameraParameters GetParameters() const;

        /// @brief カメラパラメータを設定（デフォルト実装）
        virtual void SetParameters(const CameraParameters& params);

    protected:

        bool isActive_ = true;

    };
}
