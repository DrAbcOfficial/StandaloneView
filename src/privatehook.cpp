#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <demo_api.h>
#include <ref_params.h>

#include "StandaloneViewManager.h"
#include "privatehook.h"

static_assert(METAHOOK_API_VERSION >= standaloneview::RequiredMetaHookAPIVersion,
              "StandaloneView requires gamedata resolution (MetaHook API 109)");

extern cl_enginefunc_t gEngfuncs;

namespace standaloneview
{
namespace
{
using PaintBackgroundFn = void (*)(int* extents);
// ClientPortalManager::RenderPortals and ResetAll are thiscall.
using RenderPortalsFn = void(__fastcall*)(void* pthis, int, ref_params_s* pparams);
using ResetAllFn      = int(__fastcall*)(void* pthis, int);

hook_t* renderViewHook                    = nullptr;
hook_t* paintBackgroundHook               = nullptr;
hook_t* renderPortalsHook                 = nullptr;
hook_t* resetAllHook                      = nullptr;
void (*originalRenderView)()              = nullptr;
PaintBackgroundFn originalPaintBackground = nullptr;
RenderPortalsFn   originalRenderPortals   = nullptr;
ResetAllFn        originalResetAll        = nullptr;

struct GameModule
{
    const char* name;
    PVOID       base;
};

GameModule EngineModule() { return {"engine", g_pMetaHookAPI->GetEngineBase()}; }
GameModule ClientModule() { return {"client", g_pMetaHookAPI->GetClientBase()}; }

void ReportFailure(const GameModule& module, const char* symbol, const char* reason)
{
    uint64_t   crc64  = 0;
    const auto status = g_pMetaHookAPI->GetModuleCRC64(module.base, &crc64);
    if (status == MH_GAMESYMBOL_OK)
        g_pMetaHookAPI->SysError("[StandaloneView] %s (module %s)\nEngine buildnum: %d\nCRC64: %016llx\nReason: %s",
                                 symbol, module.name, g_pMetaHookAPI->GetEngineBuildnum(), static_cast<unsigned long long>(crc64), reason);
    else
        g_pMetaHookAPI->SysError("[StandaloneView] %s (module %s)\nEngine buildnum: %d\nReason: %s\nCRC64 unavailable: %s",
                                 symbol, module.name, g_pMetaHookAPI->GetEngineBuildnum(), reason, g_pMetaHookAPI->GetGameSymbolStatusString(status));
}

template <typename T>
bool Resolve(const GameModule& module, const char* symbol, mh_gamesymbol_kind_t kind, T** address)
{
    void*      resolved = nullptr;
    const auto status   = g_pMetaHookAPI->ResolveGameSymbol(module.base, symbol, kind, &resolved);
    if (status == MH_GAMESYMBOL_OK)
    {
        *address = reinterpret_cast<T*>(resolved);
        return true;
    }
    ReportFailure(module, symbol, g_pMetaHookAPI->GetGameSymbolStatusString(status));
    return false;
}

bool IsDemoPlayback()
{
    return gEngfuncs.pDemoAPI && gEngfuncs.pDemoAPI->IsPlayingback() != 0;
}

// Hooks requested inside LoadEngine are committed after every plugin's LoadEngine
// returned; only then does MetaHook write the trampoline, so read it per call.
void __cdecl CallOriginalRenderView()
{
    originalRenderView();
}

void __cdecl RenderViewHook()
{
    StandaloneViewManager().RenderView();
}

void __cdecl PaintBackgroundHook(int* extents)
{
    StandaloneViewManager().PaintBackground(originalPaintBackground, extents);
}

// SvEngine's V_RenderView lets the Sven client render its portals before every pass.
// A standalone pass must not re-render them, and without a map the client's portal
// state is invalid.
void __fastcall RenderPortalsHook(void* pthis, int, ref_params_s* pparams)
{
    if (StandaloneViewManager().GetCurrentView())
        return;
    originalRenderPortals(pthis, 0, pparams);
}

// The client creates its portal manager on first use and resets it then, which needs
// the client's GL entry points; the client initializes them only when a map loads. A
// pass at the main menu may be that first use. HUD_VidInit resets the manager again
// for every map, so a skipped reset loses nothing.
int __fastcall ResetAllHook(void* pthis, int)
{
    if (StandaloneViewManager().GetCurrentView())
        return 0;
    return originalResetAll(pthis, 0);
}

void UninstallPortalHooks()
{
    if (resetAllHook)
    {
        g_pMetaHookAPI->UnHook(resetAllHook);
        resetAllHook = nullptr;
    }
    if (renderPortalsHook)
    {
        g_pMetaHookAPI->UnHook(renderPortalsHook);
        renderPortalsHook = nullptr;
    }
    originalResetAll      = nullptr;
    originalRenderPortals = nullptr;
}

bool InstallPortalHooks()
{
    const auto module               = ClientModule();
    void*      renderPortalsAddress = nullptr;
    void*      resetAllAddress      = nullptr;
    if (!Resolve(module, "ClientPortalManager_RenderPortals", MH_GAMESYMBOL_KIND_FUNCTION, &renderPortalsAddress) ||
        !Resolve(module, "ClientPortalManager_ResetAll", MH_GAMESYMBOL_KIND_FUNCTION, &resetAllAddress))
        return false;

    renderPortalsHook = g_pMetaHookAPI->InlineHook(renderPortalsAddress, reinterpret_cast<void*>(RenderPortalsHook),
                                                   reinterpret_cast<void**>(&originalRenderPortals));
    if (!renderPortalsHook)
    {
        ReportFailure(module, "ClientPortalManager_RenderPortals", "InlineHook failed");
        return false;
    }
    resetAllHook = g_pMetaHookAPI->InlineHook(resetAllAddress, reinterpret_cast<void*>(ResetAllHook),
                                              reinterpret_cast<void**>(&originalResetAll));
    if (!resetAllHook)
    {
        UninstallPortalHooks();
        ReportFailure(module, "ClientPortalManager_ResetAll", "InlineHook failed");
        return false;
    }
    return true;
}

bool IsSvenCoopClient()
{
    const auto clientFactory = g_pMetaHookAPI->GetClientFactory();
    return clientFactory && clientFactory("SCClientDLL001", nullptr);
}

void CalcRefdefHook(ref_params_s* pparams)
{
    StandaloneViewManager().CalcRefdef(pparams);
}

void DrawNormalTrianglesHook()
{
    StandaloneViewManager().DrawNormalTriangles();
}

void DrawTransparentTrianglesHook()
{
    StandaloneViewManager().DrawTransparentTriangles();
}
} // namespace

bool InstallEngineHooks()
{
    if (renderViewHook && paintBackgroundHook)
        return true;

    const auto   module                 = EngineModule();
    void*        renderViewAddress      = nullptr;
    void*        paintBackgroundAddress = nullptr;
    EngineBridge engine;
    if (!Resolve(module, "V_RenderView", MH_GAMESYMBOL_KIND_FUNCTION, &renderViewAddress) ||
        !Resolve(module, "VGui_ViewportPaintBackground", MH_GAMESYMBOL_KIND_FUNCTION, &paintBackgroundAddress) ||
        !Resolve(module, "cls.state", MH_GAMESYMBOL_KIND_GLOBAL, &engine.clientState) ||
        !Resolve(module, "cls.signon", MH_GAMESYMBOL_KIND_GLOBAL, &engine.clientSignon) ||
        !Resolve(module, "cl_worldmodel", MH_GAMESYMBOL_KIND_GLOBAL, &engine.worldModel) ||
        !Resolve(module, "r_worldentity", MH_GAMESYMBOL_KIND_GLOBAL, &engine.worldEntity))
        return false;

    renderViewHook = g_pMetaHookAPI->InlineHook(renderViewAddress, reinterpret_cast<void*>(RenderViewHook),
                                                reinterpret_cast<void**>(&originalRenderView));
    if (!renderViewHook)
    {
        ReportFailure(module, "V_RenderView", "InlineHook failed");
        return false;
    }
    paintBackgroundHook = g_pMetaHookAPI->InlineHook(paintBackgroundAddress, reinterpret_cast<void*>(PaintBackgroundHook),
                                                     reinterpret_cast<void**>(&originalPaintBackground));
    if (!paintBackgroundHook)
    {
        UninstallEngineHooks();
        ReportFailure(module, "VGui_ViewportPaintBackground", "InlineHook failed");
        return false;
    }

    engine.V_RenderView   = CallOriginalRenderView;
    engine.IsDemoPlayback = IsDemoPlayback;
    StandaloneViewManager().SetEngine(engine);
    return true;
}

void UninstallEngineHooks()
{
    StandaloneViewManager().ResetEngine();
    if (paintBackgroundHook)
    {
        g_pMetaHookAPI->UnHook(paintBackgroundHook);
        paintBackgroundHook = nullptr;
    }
    if (renderViewHook)
    {
        g_pMetaHookAPI->UnHook(renderViewHook);
        renderViewHook = nullptr;
    }
    originalPaintBackground = nullptr;
    originalRenderView      = nullptr;
}

bool InstallClientHooks(cl_exportfuncs_t* pExportFunc)
{
    if (!renderPortalsHook && IsSvenCoopClient() && !InstallPortalHooks())
        return false;

    ClientBridge client;
    client.V_CalcRefdef             = pExportFunc->V_CalcRefdef;
    client.DrawNormalTriangles      = pExportFunc->HUD_DrawNormalTriangles;
    client.DrawTransparentTriangles = pExportFunc->HUD_DrawTransparentTriangles;
    StandaloneViewManager().SetClient(client);

    pExportFunc->V_CalcRefdef                 = CalcRefdefHook;
    pExportFunc->HUD_DrawNormalTriangles      = DrawNormalTrianglesHook;
    pExportFunc->HUD_DrawTransparentTriangles = DrawTransparentTrianglesHook;
    return true;
}

void UninstallClientHooks()
{
    // The export table belongs to the engine and is torn down with the client;
    // only stop forwarding into the saved entry points.
    StandaloneViewManager().ResetClient();
    UninstallPortalHooks();
}
} // namespace standaloneview
