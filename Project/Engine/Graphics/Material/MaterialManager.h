#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "MathCore.h"
#include "Structs/Material.h"

/// @brief マテリアル管理クラス

namespace CoreEngine
{
	class MaterialManager {
	public: // メンバ関数

		/// @brief 初期化
		/// @param device デバイス
		/// @param resourceFactory リソース生成
		void Initialize(ID3D12Device* device, ResourceFactory* resourceFactory);

		/// @brief 色を変更
		/// @param color
		void SetColor(const Vector4& color)
		{
			materialData_->color = color; // マテリアルの色を設定
		}

		/// @brief マテリアルの色を取得
		/// @return 現在の色
		const Vector4& GetColor() const
		{
			return materialData_->color; // マテリアルの色を取得
		}

		/// @brief ライティングの有効/無効を設定
		/// @param enable 
		void SetEnableLighting(bool enable)
		{
			materialData_->enableLighting = enable; // ライティングの有効/無効を設定
		}

		/// @brief uv変換行列を設定
		/// @param uvTransform
		void SetUVTransform(const Matrix4x4& uvTransform)
		{
			materialData_->uvTransform = uvTransform; // UV変換行列を設定
		}

		/// @brief uv変換行列を取得
		/// @return
		const Matrix4x4& GetUVTransform() const
		{
			return materialData_->uvTransform; // UV変換行列を取得
		}

		/// @brief シェーディングモードを設定
		/// @param mode シェーディングモード (0: None, 1: Lambert, 2: Half-Lambert, 3: Toon)
		void SetShadingMode(int mode)
		{
			materialData_->shadingMode = mode;
		}

		/// @brief シェーディングモードを取得
		/// @return 現在のシェーディングモード
		int GetShadingMode() const
		{
			return materialData_->shadingMode;
		}

		/// @brief トゥーンシェーディングの閾値を設定
		/// @param threshold 閾値 (0.0-1.0)
		void SetToonThreshold(float threshold)
		{
			materialData_->toonThreshold = threshold;
		}

		/// @brief トゥーンシェーディングの閾値を取得
		/// @return 現在の閾値
		float GetToonThreshold() const
		{
			return materialData_->toonThreshold;
		}

		/// @brief トゥーンシェーディングの滑らかさを設定
		/// @param smoothness 滑らかさ (0.0-0.5)
		void SetToonSmoothness(float smoothness)
		{
			materialData_->toonSmoothness = smoothness;
		}

		/// @brief トゥーンシェーディングの滑らかさを取得
		/// @return 現在の滑らかさ
		float GetToonSmoothness() const
		{
			return materialData_->toonSmoothness;
		}

		/// @brief トゥーンシェーディングを有効にする（便利メソッド）
		/// @param threshold 閾値 (デフォルト: 0.5)
		/// @param smoothness 滑らかさ (デフォルト: 0.1)
		void EnableToonShading(float threshold = 0.5f, float smoothness = 0.1f)
		{
			SetShadingMode(3); // Toonモードに設定
			SetToonThreshold(threshold);
			SetToonSmoothness(smoothness);
		}

		/// @brief ディザリングを有効/無効にする
		/// @param enable true: 有効, false: 無効
		void SetEnableDithering(bool enable)
		{
			materialData_->enableDithering = enable ? 1 : 0;
		}

		/// @brief ディザリングが有効かどうかを取得
		/// @return true: 有効, false: 無効
		bool IsEnableDithering() const
		{
			return materialData_->enableDithering != 0;
		}

		/// @brief ディザリングスケールを設定
		/// @param scale スケール値（デフォルト: 1.0f、大きいほど粗いパターン）
		void SetDitheringScale(float scale)
		{
			materialData_->ditheringScale = scale;
		}

		/// @brief ディザリングスケールを取得
		/// @return 現在のスケール値
		float GetDitheringScale() const
		{
			return materialData_->ditheringScale;
		}

		/// @brief 環境マップを有効/無効にする
		/// @param enable true: 有効, false: 無効
		void SetEnableEnvironmentMap(bool enable)
		{
			materialData_->enableEnvironmentMap = enable ? 1 : 0;
		}

		/// @brief 環境マップが有効かどうかを取得
		/// @return true: 有効, false: 無効
		bool IsEnableEnvironmentMap() const
		{
			return materialData_->enableEnvironmentMap != 0;
		}

		/// @brief 環境マップの反射強度を設定
		/// @param intensity 反射強度 (0.0-1.0)
		void SetEnvironmentMapIntensity(float intensity)
		{
			materialData_->environmentMapIntensity = intensity;
		}

	/// @brief 環境マップの反射強度を取得
	/// @return 現在の反射強度
	float GetEnvironmentMapIntensity() const
	{
		return materialData_->environmentMapIntensity;
	}

	/// @brief PBRを有効/無効にする
	/// @param enable true: PBR有効, false: 従来のライティング
	void SetEnablePBR(bool enable)
	{
		materialData_->enablePBR = enable ? 1 : 0;
	}

	/// @brief PBRが有効かどうかを取得
	/// @return true: 有効, false: 無効
	bool IsEnablePBR() const
	{
		return materialData_->enablePBR != 0;
	}

	/// @brief PBRパラメータを設定
	/// @param metallic 金属性 (0.0-1.0)
	/// @param roughness 粗さ (0.0-1.0)
	/// @param ao 環境遮蔽 (0.0-1.0)
	void SetPBRParameters(float metallic, float roughness, float ao)
	{
		materialData_->metallic = metallic;
		materialData_->roughness = roughness;
		materialData_->ao = ao;
	}

	/// @brief 金属性を設定
	/// @param metallic 金属性 (0.0-1.0)
	void SetMetallic(float metallic)
	{
		materialData_->metallic = metallic;
	}

	/// @brief 金属性を取得
	/// @return 現在の金属性
	float GetMetallic() const
	{
		return materialData_->metallic;
	}

	/// @brief 粗さを設定
	/// @param roughness 粗さ (0.0-1.0)
	void SetRoughness(float roughness)
	{
		materialData_->roughness = roughness;
	}

	/// @brief 粗さを取得
	/// @return 現在の粗さ
	float GetRoughness() const
	{
		return materialData_->roughness;
	}

	/// @brief 環境遮蔽を設定
	/// @param ao 環境遮蔽 (0.0-1.0)
	void SetAO(float ao)
	{
		materialData_->ao = ao;
	}

	/// @brief 環境遮蔽を取得
	/// @return 現在の環境遮蔽
	float GetAO() const
	{
		return materialData_->ao;
	}

	/// @brief IBLを有効/無効にする
	/// @param enable true: IBL有効, false: IBL無効
	void SetEnableIBL(bool enable)
	{
		materialData_->enableIBL = enable ? 1 : 0;
	}

	/// @brief IBLが有効かどうかを取得
	/// @return true: 有効, false: 無効
	bool IsEnableIBL() const
	{
		return materialData_->enableIBL != 0;
	}

	/// @brief IBL強度を設定
	/// @param intensity IBL強度 (0.0-1.0)
	void SetIBLIntensity(float intensity)
	{
		materialData_->iblIntensity = intensity;
	}

	/// @brief IBL強度を取得
	/// @return 現在のIBL強度
	float GetIBLIntensity() const
	{
		return materialData_->iblIntensity;
	}

	/// @brief マテリアルのGPU仮想アドレスを取得
	/// @return GPU仮想アドレス
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
	{
		return materialResource_->GetGPUVirtualAddress(); // GPU仮想アドレスを取得
	}

		/// @brief マテリアルデータを取得
		/// @return
		Material* GetMaterialData() const
		{
			return materialData_; // マテリアルデータを取得
		}

	private: // メンバ変数
		// マテリアルリソース
		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
		// マテリアルデータ
		Material* materialData_ = nullptr;
	};
}
