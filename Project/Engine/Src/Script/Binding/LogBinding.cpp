#include "pch.h"
#include "Script/Binding/LogBinding.h"

#include "Utility/Logger/Logger.h"

#include <angelscript.h>

#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        void WriteLog(LogLevel level, const std::string& message)
        {
            Logger::GetInstance().Logf(level, LogCategory::Script, "{}", message);
        }

        void LogInfo(const std::string& message)
        {
            WriteLog(LogLevel::Info, message);
        }

        void LogWarn(const std::string& message)
        {
            WriteLog(LogLevel::Warn, message);
        }

        void LogError(const std::string& message)
        {
            WriteLog(LogLevel::Error, message);
        }
    }

    bool RegisterLogBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        const bool info = engine->RegisterGlobalFunction(
            "void Log(const string &in message)", asFUNCTION(LogInfo), asCALL_CDECL) >= 0;
        const bool warn = engine->RegisterGlobalFunction(
            "void Warn(const string &in message)", asFUNCTION(LogWarn), asCALL_CDECL) >= 0;
        const bool error = engine->RegisterGlobalFunction(
            "void Error(const string &in message)", asFUNCTION(LogError), asCALL_CDECL) >= 0;
        return info && warn && error;
    }
}
