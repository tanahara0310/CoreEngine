#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"
#include <string>

namespace CoreEngine
{
	/// @brief 実行時にモデルパスを指定できる汎用モデルオブジェクト
	///
	/// ProjectViewのドラッグ＆ドロップや実行時スポーンなど、
	/// コンパイル時にモデルパスが決まらないケースで使用する。
	class DynamicModelObject : public ModelGameObject {
	public:
		/// @brief モデルファイルパスを設定する（Initialize前に呼ぶこと）
		/// @param modelPath モデルファイル名（例: "cube.obj"）
		void SetModelPath(const std::string& modelPath) { modelPath_ = modelPath; }

		/// @brief 設定されているモデルファイルパスを返す
		const std::string& GetDynamicModelPath() const { return modelPath_; }

		/// @brief オブジェクト種別名を返す
		const char* GetObjectName() const override { return "DynamicModel"; }

	protected:
		std::string GetModelPath() const override { return modelPath_; }

	private:
		std::string modelPath_;
	};
}
