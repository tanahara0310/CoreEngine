#pragma once

#include "externals/nlohmann/single_include/nlohmann/json_fwd.hpp"
#include <cstdint>

/// @brief エディタ設定自動保存のセクションインターフェース

namespace CoreEngine
{
    class EditorSettingsSubsystem;

    /// @brief エディタ設定自動保存の 1 セクション（＝1 ファイル）を表すインターフェース
    /// @details EditorSettingsSubsystem に登録すると、登録時に保存済み JSON から Deserialize され、
    ///          以降はポーリング差分検知により変更のたびに自動保存される。
    ///          シーン/GameObject の明示保存（SceneSaveSystem）とは独立した仕組み。
    ///          設計書: Docs/Engine/Editor/EditorSettingsAutoSave_Design.md
    class IEditorSettingsSection
    {
    public:
        /// @note 登録されたまま破棄された場合はストアから自己解除する
        ///       （定義は EditorSettingsSubsystem.cpp）
        virtual ~IEditorSettingsSection();

        /// @brief セクション名（保存ファイル名 "{name}.json" になる）
        virtual const char* GetSectionName() const = 0;

        /// @brief 現在の設定値を JSON へ書き出す
        /// @param out 書き出し先 JSON オブジェクト
        virtual void Serialize(nlohmann::json& out) const = 0;

        /// @brief JSON から設定値を復元する
        /// @param in 読み込み元 JSON オブジェクト
        /// @note JsonManager::SafeGet を使い、欠損キーは現在値を維持すること（後方互換）
        virtual void Deserialize(const nlohmann::json& in) = 0;

        // ──────────────────────────────────────────────────────────
        // 保存先の区分（設計書: Docs/Engine/Editor/InstantSettingsSave_Design.md Phase 4）
        // ──────────────────────────────────────────────────────────

        /// @brief 保存先の区分（UE の Config/ と Saved/Config/ の分離に相当）
        enum class StorageArea
        {
            /// 個人の作業状態（カメラ位置・ウィンドウ配置等）。
            /// Application/Saved/ 配下＝git 管理外。人ごとに違ってよい値
            UserSaved,
            /// プロジェクト設定（水・大気などの較正値）。
            /// Application/Config/ 配下＝git にコミットしてチームで共有する値
            ProjectConfig,
        };

        /// @brief このセクションの保存先区分（既定は個人状態）
        virtual StorageArea GetStorageArea() const { return StorageArea::UserSaved; }

        // ──────────────────────────────────────────────────────────
        // 変更検知の方式（設計書: Docs/Engine/Editor/InstantSettingsSave_Design.md）
        // ──────────────────────────────────────────────────────────

        /// @brief 変更検知の方式
        enum class ChangeSignal
        {
            /// 1 秒ごとに全量シリアライズして前回保存分と比較する。
            /// 変更イベントを持てないセクション用（実体が毎フレーム CVar へ書き込む
            /// ミラー系: DebugCamera / AtmosphereLights 等）
            Polling,

            /// GetChangeRevision() の整数比較で毎フレーム安価に検知する（イベント駆動）。
            /// 変更は 0.3 秒のデバウンス後、確定（GetCommitRevision の変化）は即時保存される
            Revision,
        };

        /// @brief このセクションの変更検知方式（既定はポーリング）
        virtual ChangeSignal GetChangeSignal() const { return ChangeSignal::Polling; }

        /// @brief 値が変更されるたびに増える通番（Revision 方式のみ意味を持つ）
        /// @note 毎フレーム呼ばれるため O(1) であること
        virtual uint64_t GetChangeRevision() const { return 0; }

        /// @brief 編集が「確定」する（スライダーを離す等）たびに増える通番
        /// @details これが進んだフレームはデバウンスを待たず即時保存される
        virtual uint64_t GetCommitRevision() const { return 0; }

    private:
        friend class EditorSettingsSubsystem;

        // 登録先ストア（RegisterSection / UnregisterSections が設定・解除する）。
        // 所有者が解除し忘れたままセクションが破棄されても、デストラクタがここ経由で
        // 自己解除するため、ストア側にダングリングポインタが残らない
        EditorSettingsSubsystem* registeredStore_ = nullptr;
    };
}
