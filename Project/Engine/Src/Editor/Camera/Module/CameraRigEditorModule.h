#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"
#include "Camera/Rig/CameraRigTypes.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief カメラリグ（状態駆動のカメラワーク）を編集するモジュール
    ///
    /// @details
    /// キーフレームが「時刻 → 構図」なのに対し、リグは「シーンの状態 → 構図」。
    /// 追従・注視・視野角の決め方をここで組めば、TPS の追従や「プレイヤーと敵を
    /// まとめて画に収める」カメラがコードを書かずに作れる。
    ///
    /// 動かしている間は編集内容がそのまま実機へ流れる（CameraRigRuntime::ReplaceAsset）。
    /// 値をいじった結果がその場のゲーム画面で分かるので、ビルドを待たずに詰められる。
    class CameraRigEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "リグ"; }

        /// @brief 毎フレーム更新（動かしている間、編集中の値を実機へ流す）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        /// @brief リグの選択・出し入れ・保存の行を描画（常に最上段で位置が動かない）
        void DrawRigSelector(const CameraEditorContext& context);

        /// @brief 動かす / 止める と、いま誰がカメラを握っているかを描画
        void DrawTransport();

        /// @brief 位置 (Body) の設定を描画
        void DrawBody(const CameraEditorContext& context);

        /// @brief 向き (Aim) の設定を描画
        void DrawAim(const CameraEditorContext& context);

        /// @brief 画角 (Lens) の設定を描画
        void DrawLens();

        /// @brief 減衰の設定を描画
        void DrawDamping();

        /// @brief 対象 1 件の指定（名前・オフセット）を描画
        /// @param showWeight まとめて収める対象なら重みも出す
        /// @return 値が変わったら true
        bool DrawTargetRef(const char* label, CameraRigTargetRef& target,
            const CameraEditorContext& context, bool showWeight);

        /// @brief 対象の並び（追加・削除つき）を描画
        /// @return 値が変わったら true
        bool DrawTargetList(CameraRigTargetRef* singleUnused,
            std::vector<CameraRigTargetRef>& targets, const CameraEditorContext& context);

        /// @brief シーンのオブジェクト名を選ぶコンボを描画
        /// @return 選び直したら true
        bool DrawObjectPicker(const char* label, std::string& objectName,
            const CameraEditorContext& context);

        /// @brief 保存先ディレクトリのファイル一覧を取り直す
        void RefreshRigFileList();

        /// @brief 名前から保存パスを組む
        std::string MakeRigPath(const std::string& name) const;

        /// @brief 編集中のリグをファイルへ書き出す
        bool SaveCurrentRig();

        /// @brief ファイルから読み込んで編集対象にする
        bool LoadRig(const std::string& name);

        /// @brief 動かしている実行時リグへ編集内容を流す
        void PushToRuntime();

        // 編集中のリグ本体
        CameraRigAsset rig_;

        // 保存名。ファイル名にもリグ名にもなる。
        char nameBuffer_[128] = "NewCameraRig";

        // 一覧（拡張子なし・昇順）
        std::vector<std::string> rigFileList_;
        int selectedRigIndex_ = -1;
        bool needRefreshRigFileList_ = true;

        // 動かすときの繋ぎ時間 [秒]
        float activateBlendSeconds_ = 0.5f;

        // 編集して以降まだ実機へ流していないか
        bool dirty_ = false;

        // 最後の操作結果（保存できた・読めなかった等）
        std::string statusMessage_;
    };
}

#endif // USE_IMGUI
