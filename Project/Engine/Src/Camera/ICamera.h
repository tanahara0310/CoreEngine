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

        /// @brief ビュー行列と視点位置を一時的に差し替える（平面反射などの補助ビュー描画用）
        /// @details 差し替え中も カメラ自身の transform / 入力状態は変更しない。
        ///          EndViewOverride() で即座に元のビューへ戻る。
        ///          対応していないカメラ実装では false を返す（呼び出し側は
        ///          差し替え無しで描画される前提のフォールバックを行うこと）。
        /// @param viewMatrix 差し替え後のビュー行列
        /// @param viewPosition 差し替え後の視点ワールド座標（GPU の gCamera.worldPosition に反映）
        /// @param projectionOverride 差し替え後の射影行列（斜交近クリップ等）。nullptr なら通常の射影を使う
        /// @return 差し替えが適用された場合 true
        virtual bool BeginViewOverride(
            const Matrix4x4& viewMatrix,
            const Vector3& viewPosition,
            const Matrix4x4* projectionOverride = nullptr) {
            (void)viewMatrix;
            (void)viewPosition;
            (void)projectionOverride;
            return false;
        }

        /// @brief BeginViewOverride() によるビュー差し替えを解除する
        virtual void EndViewOverride() {}

    protected:

        bool isActive_ = true;

        // ===== BeginViewOverride() 用の共有状態 =====
        // 対応するカメラ実装（Camera / DebugCamera）は GetViewMatrix() / GetProjectionMatrix() /
        // GetPosition() / TransferMatrix() でこの状態を参照する。
        bool viewOverrideActive_ = false;
        bool projectionOverrideActive_ = false;
        Matrix4x4 overrideViewMatrix_{};
        Matrix4x4 overrideProjectionMatrix_{};
        Vector3 overrideViewPosition_{ 0.0f, 0.0f, 0.0f };

    };
}
