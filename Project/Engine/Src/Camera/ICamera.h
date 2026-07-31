#pragma once
#include <Math/MathCore.h>
#include <Math/Frustum.h>
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

        /// @brief カメラの視錐台（Frustum）を取得
        /// @return VP行列から抽出した視錐台
        virtual Frustum GetFrustum() const {
            Frustum frustum;
            Matrix4x4 vp = GetViewMatrix() * GetProjectionMatrix();
            frustum.ExtractFromMatrix(vp);
            return frustum;
        }

        /// @brief TAA 用のサブピクセルジッタ（NDC 単位）を設定する
        /// @details 射影行列へジッタを加えた写しを内部に作り、以降 GetProjectionMatrix() が
        ///          そちらを返すようにする。モデルの WVP・深度復元用の invViewProj・視錐台が
        ///          すべて同じジッタ付き行列から導かれるため、フレーム内で整合が取れる。
        ///
        ///          ジッタは「毎フレーム設定し直す」前提。射影行列が作り直された場合も
        ///          次の呼び出しで最新のものから作り直される。
        /// @param ndcX NDC の X オフセット（1 ピクセル = 2 / 画面幅）
        /// @param ndcY NDC の Y オフセット
        void SetProjectionJitter(float ndcX, float ndcY);

        /// @brief 現在適用中のジッタ（NDC）を取得する
        float GetProjectionJitterX() const { return projectionJitterX_; }
        float GetProjectionJitterY() const { return projectionJitterY_; }

    protected:

        /// @brief ジッタを適用する前の射影行列を返す
        /// @details ジッタへ対応するカメラは生の projectionMatrix_ を返すこと。
        ///          nullptr を返す実装（既定）ではジッタは無効化される。
        virtual const Matrix4x4* GetUnjitteredProjectionMatrix() const { return nullptr; }

        bool isActive_ = true;

        // ===== SetProjectionJitter() 用の共有状態 =====
        // 対応するカメラ実装は GetProjectionMatrix() でこの状態を参照する。
        bool projectionJitterActive_ = false;
        float projectionJitterX_ = 0.0f;
        float projectionJitterY_ = 0.0f;
        Matrix4x4 jitteredProjectionMatrix_{};

    };
}
