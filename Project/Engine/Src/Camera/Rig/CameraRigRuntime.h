#pragma once

#include "Camera/Rig/CameraRigEvaluator.h"

#include <cstdint>
#include <memory>
#include <string>

/// @file
/// @brief 動作中のカメラリグ 1 本を進める実行時状態

namespace CoreEngine
{
    /// @brief リグを動かし始めるときの指定
    struct CameraRigActivateOptions {
        /// @brief 今の構図から何秒かけて繋ぐか（0 で即座に切り替え）
        /// @details リグを切り替える瞬間にカメラが飛ぶのを防ぐ。繋ぎ元は
        ///          「切り替えた瞬間のカメラ姿勢」なので、前のリグから次のリグへも、
        ///          ゲーム側の追従から最初のリグへも、同じ指定で繋がる。
        float blendSeconds = 0.0f;

        /// @brief 繋ぎの進行にスケールされない時間を使うか
        /// @details ヒットストップ中でもリグの切り替えを進めたいときに立てる。
        bool useUnscaledTime = false;

        /// @brief 動かし始めに減衰の状態を捨てるか
        /// @details true なら初回フレームで理想の姿勢へ直接飛ぶ（繋ぎで滑らかにする前提）。
        ///          false だと前のリグの位置から減衰で寄っていく。
        bool resetState = true;
    };

    /// @brief 動作中のリグ 1 本ぶんの状態
    ///
    /// @details
    /// 保持するのは「どのリグを」「どこまで繋いだか」「減衰の続き」の 3 つだけ。
    /// カメラにも Camera クラスにも触らない。書き込みは CameraRigFeature の仕事。
    ///
    /// シーケンスの CameraSequencePlayer と同じ役どころだが、こちらには再生ヘッドが無い。
    /// リグは終わらないので、Stopped / Playing / Finishing のような状態も持たない。
    ///
    /// @note メインスレッド専用。
    class CameraRigRuntime {
    public:
        /// @brief リグを動かし始める
        /// @param asset 動かすリグ（nullptr なら何もせず止まったまま）
        /// @param options 繋ぎ方
        /// @param name ログや UI に出す名前
        void Activate(std::shared_ptr<const CameraRigAsset> asset,
            const CameraRigActivateOptions& options, const std::string& name);

        /// @brief リグを止め、カメラをゲーム側へ返す
        void Deactivate();

        /// @brief リグがカメラを握っているか
        bool IsActive() const { return asset_ != nullptr; }

        /// @brief 動作中のリグ名（止まっていれば空）
        const std::string& GetName() const { return name_; }

        /// @brief 動作中のリグ（止まっていれば nullptr）
        const CameraRigAsset* GetAsset() const { return asset_.get(); }

        /// @brief 繋ぎの重み（0 = 繋ぎ元 / 1 = リグの出力そのもの）
        float GetBlendWeight() const { return blendWeight_; }

        /// @brief 動かし始めるたびに増える通し番号
        /// @details 呼び出し側が「新しい切り替えが始まったフレーム」を見分けるための印。
        ///          同じリグを繋ぎ直したときも変わる。止まっている間は 0。
        std::uint32_t GetActivationId() const { return activationId_; }

        /// @brief 繋ぎを進める（カメラの有無に関わらず毎フレーム呼ぶ）
        void Update(float deltaTime, float unscaledDeltaTime);

        /// @brief 今フレームの構図を求め、減衰の状態を進める
        /// @param deltaTime 減衰に使う経過時間 [秒]
        /// @param context 対象の解決口
        /// @param inOutSnapshot 呼び出し側が渡した姿勢を、位置・回転・視野角だけ書き換える
        /// @return 止まっている / 対象を解決できないなら false（呼び出し側はカメラを触らないこと）
        /// @details 近クリップ・遠クリップ・アスペクト比には触らない。カメラの現在値を
        ///          そのまま渡せば、リグが決める 3 つだけが差し替わる。
        bool Evaluate(float deltaTime, const CameraRigContext* context, CameraSnapshot& inOutSnapshot);

        /// @brief 動作中のリグの中身だけ差し替える
        /// @details 繋ぎの重みも減衰の続きも保つので、切り替え直しにはならない。
        ///          エディタで値をいじりながら結果を見るためのもの。止まっている間は何もしない。
        void ReplaceAsset(std::shared_ptr<const CameraRigAsset> asset);

        /// @brief 減衰の状態を捨てる（次フレームは理想の姿勢へ直接置かれる）
        void ResetState() { state_.Reset(); }

    private:
        std::shared_ptr<const CameraRigAsset> asset_;
        std::string name_;
        CameraRigActivateOptions options_{};
        CameraRigState state_{};

        float blendWeight_ = 1.0f;
        std::uint32_t activationId_ = 0;
    };
}
