#include "pch.h"
#include "Script/ScriptDiagnostics.h"

#include "Utility/Logger/Logger.h"

#include <angelscript.h>

namespace CoreEngine::Script
{
    void OnCompilerMessage(const asSMessageInfo* message, void* userData)
    {
        (void)userData;
        if (!message) {
            return;
        }

        LogLevel level = LogLevel::Info;
        if (message->type == asMSGTYPE_ERROR) {
            level = LogLevel::Error;
        } else if (message->type == asMSGTYPE_WARNING) {
            level = LogLevel::Warn;
        }

        Logger::GetInstance().Logf(level, LogCategory::Script, "{}({}, {}) : {}",
            message->section ? message->section : "", message->row, message->col,
            message->message ? message->message : "");
    }

    void LogFailedExecution(asIScriptContext* context, const std::string& headline)
    {
        Logger& logger = Logger::GetInstance();
        logger.Logf(LogLevel::Error, LogCategory::Script, "{}", headline);
        if (!context) {
            return;
        }

        if (context->GetState() == asEXECUTION_EXCEPTION) {
            int column = 0;
            const char* section = nullptr;
            const int row = context->GetExceptionLineNumber(&column, &section);
            const char* exception = context->GetExceptionString();
            logger.Logf(LogLevel::Error, LogCategory::Script, "  例外: {} @ {}({}, {})",
                exception ? exception : "", section ? section : "", row, column);
        }

        for (asUINT level = 0; level < context->GetCallstackSize(); ++level) {
            const asIScriptFunction* function = context->GetFunction(level);
            int column = 0;
            const char* section = nullptr;
            const int row = context->GetLineNumber(level, &column, &section);
            logger.Logf(LogLevel::Error, LogCategory::Script, "  #{} {} @ {}({}, {})",
                level, function ? function->GetDeclaration() : "?", section ? section : "", row, column);
        }
    }
}
