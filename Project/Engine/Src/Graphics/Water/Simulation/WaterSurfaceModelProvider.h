#pragma once

#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
	/// @brief 水面モデル由来の DXR 用 surface data を供給する抽象インターフェース
	class IWaterSurfaceModelProvider {
	public:
		virtual ~IWaterSurfaceModelProvider() = default;
        /// @brief 現在の水面状態を取り出す（未準備なら false を返し outSurfaceData は触らない）
		virtual bool TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const = 0;
        /// @brief この供給元が扱う波の方式（Gerstner / FFT）
		virtual WaterSurfaceSimulationType GetSimulationType() const = 0;
        /// @brief 診断ログ・デバッグ UI に出す供給元名
		virtual const char* GetProviderName() const = 0;
	};

	/// @brief WaterSurfaceData のスナップショットを供給する軽量プロバイダー
	/// @details **値で保持する。** 以前は外部が持つ実体への生ポインタを握っており、
	///          RT マネージャが握る shared_ptr のコピーが供給元（シーン側のメンバ）より
	///          長生きするとダングリングになる構造だった。値コピー（1 フレーム分の
	///          スナップショット）にすることで、供給元の寿命から完全に切り離す。
	class StaticWaterSurfaceModelProvider final : public IWaterSurfaceModelProvider {
	public:
        /// @brief 診断ログに出す供給元名を指定して構築する
		explicit StaticWaterSurfaceModelProvider(const char* providerName);

		/// @brief このフレームの surface data を差し替える
		void SetSurfaceData(
			const WaterSurfaceData& surfaceData,
			WaterSurfaceSimulationType simulationType);

		/// @brief 「水面なし」状態にする（TryGetSurfaceData が false を返すようになる）
		void ClearSurfaceData();

		bool TryGetSurfaceData(WaterSurfaceData& outSurfaceData) const override;
		WaterSurfaceSimulationType GetSimulationType() const override;
		const char* GetProviderName() const override;

	private:
		WaterSurfaceData surfaceData_{};
		bool hasSurfaceData_ = false;
		WaterSurfaceSimulationType simulationType_ = WaterSurfaceSimulationType::Gerstner;
		const char* providerName_ = "StaticWaterSurfaceModelProvider";
	};
}
