#include "pch.h"
#include "LivePPAgent.h"

#ifdef _DEBUG
#include <Windows.h>

namespace CoreEngine
{

LivePPAgent::~LivePPAgent()
{
    if (IsValid())
    {
        lpp::LppDestroySynchronizedAgent(&agent_);
    }
}

void LivePPAgent::Initialize()
{
    KillBrokerProcess();

    agent_ = lpp::LppCreateSynchronizedAgent(nullptr, L"Externals/LivePP");
    if (IsValid())
    {
        agent_.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_NONE, nullptr, nullptr);
    }
}

void LivePPAgent::Update()
{
    if (!IsValid()) { return; }

    if (agent_.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_RELOAD))
    {
        agent_.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
    }
    if (agent_.WantsRestart())
    {
        agent_.Restart(lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0u, nullptr);
    }
}

bool LivePPAgent::IsValid() const
{
    return lpp::LppIsValidSynchronizedAgent(&agent_);
}

void LivePPAgent::KillBrokerProcess()
{
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"taskkill /IM LPP_Broker.exe /F";
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

}

#endif // _DEBUG
