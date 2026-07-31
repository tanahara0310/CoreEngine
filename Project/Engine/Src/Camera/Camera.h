#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Math/MathCore.h"
#include "Math/Frustum.h"
#include "Camera/CameraStructs.h"

/// @brief カメラ（エンジン唯一の具象カメラ）

namespace CoreEngine {

    /// @brief カメラ
    /// @details 継承階層を持たない「データ + 行列導出」だけのクラス。
    ///          以前は ICamera / Camera / DebugCamera / Camera2D の 4 型があり、
    ///          姿勢の表現（SRT vs 軌道）も投影方式もクラスの違いとして持っていたため、
    ///          具象型への dynamic_cast が 21 箇所に散っていた。
    ///
    ///          - 投影方式は CameraParameters::projectionType で表す（2D は正射影）
    ///          - 操作方法（Blender 風の軌道操作・追従など）はカメラではなく
    ///            コントローラ（OrbitFlyController 等）が持ち、結果を Transform へ書き込む
    class Camera final {
    public:
        Camera() = default;
        explicit Camera(const CameraParameters& parameters) : parameters_(parameters) {}

        /// @brief GPU リソースの初期化
        /// @param device D3D12デバイス（nullptr なら GPU 定数バッファを持たない）
        void Initialize(ID3D12Device* device);

        /// @brief 行列の更新 + GPU 転送（フレームごとに 1 回）
        void Update();

        /// @brief 行列の更新（GPU 転送を含む）
        void UpdateMatrix();

        // ====== 行列・視点 ======

        /// @brief ビュー行列を取得
        const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }

        /// @brief 射影行列を取得（TAA ジッタ適用後）
        const Matrix4x4& GetProjectionMatrix() const {
            return projectionJitterActive_ ? jitteredProjectionMatrix_ : projectionMatrix_;
        }

        /// @brief カメラのワールド座標を取得
        Vector3 GetPosition() const { return translate_; }

        /// @brief カメラ行列（ビュー行列の逆）を取得
        const Matrix4x4& GetCameraMatrix() const { return cameraMatrix_; }

        /// @brief 視錐台を取得
        /// @note 毎フレーム多数回呼ぶ用途では ViewInfo::frustum（構築時に 1 回抽出）を使うこと
        Frustum GetFrustum() const {
            Frustum frustum;
            frustum.ExtractFromMatrix(MathCore::Matrix::Multiply(GetViewMatrix(), GetProjectionMatrix()));
            return frustum;
        }

        // ====== GPU リソース ======

        /// @brief カメラ用定数バッファ（CameraForGPU）の GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
            return cameraGPUResource_ ? cameraGPUResource_->GetGPUVirtualAddress() : 0;
        }

        /// @brief 視点ワールド座標を GPU へ転送する
        /// @details 定数バッファは単一のアップロードバッファ。フレーム内で複数回書き換えると
        ///          実行中の前フレーム（インフライト）がどちらを読むかタイミング依存になり、
        ///          カメラ位置依存のフレネル項がちらつく。書き込みはフレーム 1 回に限ること。
        void TransferMatrix();

        // ====== パラメータ ======

        CameraParameters GetParameters() const { return parameters_; }
        void SetParameters(const CameraParameters& params) { parameters_ = params; }
        void ResetParameters() { parameters_.Reset(); }

        /// @brief カメラタイプ（投影方式から決まる）
        CameraType GetCameraType() const { return parameters_.GetCameraType(); }

        /// @brief 有効/無効状態
        void SetActive(bool isActive) { isActive_ = isActive; }
        bool GetActive() const { return isActive_; }

        // ====== Transform ======

        void SetScale(const Vector3& scale) { scale_ = scale; }
        void SetRotate(const Vector3& rotate) { rotate_ = rotate; }
        void SetTranslate(const Vector3& translate) { translate_ = translate; }

        Vector3 GetScale() const { return scale_; }
        Vector3 GetRotate() const { return rotate_; }
        Vector3 GetTranslate() const { return translate_; }

        /// @brief 2D カメラのズーム倍率を設定（正射影時のみ意味を持つ）
        void SetZoom(float zoom) { scale_ = { zoom, zoom, 1.0f }; }

        /// @brief 2D カメラのズーム倍率を取得
        float GetZoom() const { return scale_.x; }

        /// @brief 指定した位置を注視するようにカメラを回転
        /// @param target 注視点
        void LookAt(const Vector3& target);

        /// @brief カメラの前方向ベクトル（正規化済み）
        Vector3 GetForward() const;

        /// @brief カメラの右方向ベクトル（正規化済み）
        Vector3 GetRight() const;

        /// @brief カメラの上方向ベクトル（正規化済み）
        Vector3 GetUp() const;

        /// @brief カメラを初期状態に戻す
        void Reset();

        // ====== TAA ジッタ ======

        /// @brief TAA 用のサブピクセルジッタ（NDC 単位）を設定する
        /// @details 射影行列へジッタを加えた写しを内部に作り、以降 GetProjectionMatrix() が
        ///          そちらを返す。ViewBuilder がこの後にスナップショットを取ることで、
        ///          モデルの WVP・深度復元の invViewProj・視錐台がすべて同じ行列から導かれる。
        /// @param ndcX NDC の X オフセット（1 ピクセル = 2 / 画面幅）
        /// @param ndcY NDC の Y オフセット
        void SetProjectionJitter(float ndcX, float ndcY);

        float GetProjectionJitterX() const { return projectionJitterX_; }
        float GetProjectionJitterY() const { return projectionJitterY_; }

        // ====== スナップショット ======

        /// @brief 現在の状態をスナップショットとして保存
        CameraSnapshot CaptureSnapshot(const std::string& name = "Snapshot") const;

        /// @brief スナップショットから状態を復元
        void RestoreSnapshot(const CameraSnapshot& snapshot);

    private:
        /// @brief 投影方式に応じて view / projection を作り直す
        void RebuildMatrices();

        /// @brief パラメータからアスペクト比を解決する（0 指定ならウィンドウから自動）
        float ResolveAspectRatio() const;

        // Transform（正射影時は translate=中心座標 / rotate.z=回転 / scale.x=ズーム）
        Vector3 scale_ = { 1.0f, 1.0f, 1.0f };
        Vector3 rotate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 translate_ = { 0.0f, 0.0f, -10.0f };

        Matrix4x4 viewMatrix_{};
        Matrix4x4 projectionMatrix_{};
        Matrix4x4 cameraMatrix_{};

        CameraParameters parameters_{};
        bool isActive_ = true;

        // TAA ジッタ
        bool projectionJitterActive_ = false;
        float projectionJitterX_ = 0.0f;
        float projectionJitterY_ = 0.0f;
        Matrix4x4 jitteredProjectionMatrix_{};

        // GPU 用リソース（CameraForGPU = 視点ワールド座標）
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraGPUResource_;
        CameraForGPU* cameraGPUData_ = nullptr;
    };
}
