#pragma once
#include <string>
#include <format>
#include <Windows.h>

/// @brief ファイル読み込みエラーをメッセージボックスで表示するユーティリティ

namespace CoreEngine
{
namespace FileErrorDialog {

	/// @brief テクスチャ読み込みエラーを表示
	/// @param message エラーメッセージ
	/// @param filePath ファイルパス
	/// @param hr HRESULTコード（オプション）
	inline void ShowTextureError(const std::string& message, const std::string& filePath, HRESULT hr = S_OK)
	{
		std::string errorText;
		
		if (hr != S_OK) {
			errorText = std::format(
				"TEXTURE LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"HRESULT: 0x{:08X}\n\n"
				"Please check if the file exists and the path is correct.",
				message, filePath, static_cast<unsigned int>(hr)
			);
		} else {
			errorText = std::format(
				"TEXTURE LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Please check if the file exists and the path is correct.",
				message, filePath
			);
		}
		
		MessageBoxA(nullptr, errorText.c_str(), "Texture Loading Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}

	/// @brief モデル読み込みエラーを表示
	/// @param message エラーメッセージ
	/// @param filePath ファイルパス
	/// @param assimpError Assimpエラーメッセージ（オプション）
	inline void ShowModelError(const std::string& message, const std::string& filePath, const std::string& assimpError = "")
	{
		std::string errorText;
		
		if (!assimpError.empty()) {
			errorText = std::format(
				"MODEL LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Assimp Error:\n{}\n\n"
				"Please check if the file exists and the format is supported.",
				message, filePath, assimpError
			);
		} else {
			errorText = std::format(
				"MODEL LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Please check if the file exists and the format is supported.",
				message, filePath
			);
		}
		
		MessageBoxA(nullptr, errorText.c_str(), "Model Loading Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}

	/// @brief オーディオファイル読み込みエラーを表示
	/// @param message エラーメッセージ
	/// @param filePath ファイルパス
	/// @param hr HRESULTコード（オプション）
	inline void ShowAudioError(const std::string& message, const std::string& filePath, HRESULT hr = S_OK)
	{
		std::string errorText;
		
		if (hr != S_OK) {
			errorText = std::format(
				"AUDIO LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"HRESULT: 0x{:08X}\n\n"
				"Please check if the file exists and the format is supported.",
				message, filePath, static_cast<unsigned int>(hr)
			);
		} else {
			errorText = std::format(
				"AUDIO LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Please check if the file exists and the format is supported.",
				message, filePath
			);
		}
		
		MessageBoxA(nullptr, errorText.c_str(), "Audio Loading Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}

	/// @brief 汎用ファイル読み込みエラーを表示
	/// @param fileType ファイルタイプ（例: "Shader", "JSON"）
	/// @param message エラーメッセージ
	/// @param filePath ファイルパス
	/// @param additionalInfo 追加情報（オプション）
	inline void ShowFileError(const std::string& fileType, const std::string& message, const std::string& filePath, const std::string& additionalInfo = "")
	{
		std::string errorText;
		
		if (!additionalInfo.empty()) {
			errorText = std::format(
				"{} LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Additional Info:\n{}\n\n"
				"Please check if the file exists and the path is correct.",
				fileType, message, filePath, additionalInfo
			);
		} else {
			errorText = std::format(
				"{} LOADING ERROR\n\n"
				"Message:\n{}\n\n"
				"File Path:\n{}\n\n"
				"Please check if the file exists and the path is correct.",
				fileType, message, filePath
			);
		}
		
		std::string title = fileType + " Loading Error";
		MessageBoxA(nullptr, errorText.c_str(), title.c_str(), MB_OK | MB_ICONERROR | MB_TOPMOST);
	}

} // namespace FileErrorDialog
}
