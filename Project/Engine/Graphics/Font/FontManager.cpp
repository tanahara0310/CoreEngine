#include "FontManager.h"
#include "Font.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Utility/Logger/Logger.h"


namespace CoreEngine
{
FontManager& FontManager::GetInstance() {
    static FontManager instance;
    return instance;
}

bool FontManager::Initialize(DirectXCommon* dxCommon) {
    if (isInitialized_) {
        Logger::GetInstance().Log("FontManager is already initialized");
        return true;
    }

    dxCommon_ = dxCommon;

    if (FT_Init_FreeType(&ftLibrary_)) {
        Logger::GetInstance().Log("Failed to initialize FreeType library");
        return false;
    }

    isInitialized_ = true;
    Logger::GetInstance().Log("FontManager initialized successfully");
    return true;
}

void FontManager::Finalize() {
    if (!isInitialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    fontCache_.clear();

    if (ftLibrary_) {
        FT_Done_FreeType(ftLibrary_);
        ftLibrary_ = nullptr;
    }

    isInitialized_ = false;
    Logger::GetInstance().Log("FontManager finalized");
}

Font* FontManager::LoadFont(const std::string& fontFilePath, uint32_t fontSize) {
    if (!isInitialized_) {
        Logger::GetInstance().Log("FontManager is not initialized");
        return nullptr;
    }

    std::string key = GenerateFontKey(fontFilePath, fontSize);

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = fontCache_.find(key);
        if (it != fontCache_.end()) {
            return it->second.get();
        }
    }

    auto font = std::make_unique<Font>();
    if (!font->Initialize(ftLibrary_, fontFilePath, fontSize, dxCommon_)) {
        return nullptr;
    }

    Font* result = font.get();

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        fontCache_[key] = std::move(font);
    }

    return result;
}

void FontManager::SetDefaultFont(const std::string& fontFilePath, uint32_t fontSize) {
    defaultFontKey_ = GenerateFontKey(fontFilePath, fontSize);
    LoadFont(fontFilePath, fontSize);
}

Font* FontManager::GetDefaultFont() {
    if (defaultFontKey_.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = fontCache_.find(defaultFontKey_);
    if (it != fontCache_.end()) {
        return it->second.get();
    }

    return nullptr;
}

std::string FontManager::GenerateFontKey(const std::string& fontFilePath, uint32_t fontSize) const {
    return fontFilePath + "_" + std::to_string(fontSize);
}
}
