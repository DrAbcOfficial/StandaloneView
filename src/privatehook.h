#pragma once

namespace standaloneview
{
// ResolveGameSymbol / GetModuleCRC64 (API 109).
constexpr int RequiredMetaHookAPIVersion = 109;
bool          InstallEngineHooks();
void          UninstallEngineHooks();
bool          InstallClientHooks(struct cl_exportfuncs_s* pExportFunc);
void          UninstallClientHooks();
} // namespace standaloneview
