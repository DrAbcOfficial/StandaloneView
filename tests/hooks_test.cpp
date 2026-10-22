#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <const.h>
#include <r_studioint.h>
#include <ref_params.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "StandaloneViewManager.h"
#include "privatehook.h"

metahook_api_t* g_pMetaHookAPI = nullptr;
cl_enginefunc_t gEngfuncs      = {};

namespace
{
using RenderViewFn      = void (*)();
using PaintBackgroundFn = void (*)(int*);
using RenderPortalsFn   = void(__fastcall*)(void*, int, ref_params_s*);
using ResetAllFn        = int(__fastcall*)(void*, int);

int                    failures       = 0;
int                    installs       = 0;
int                    resolves       = 0;
int                    engineResolves = 0;
int                    failInstall    = 0;
int                    mainRenders    = 0;
int                    paints         = 0;
int                    portalRenders  = 0;
int                    portalResets   = 0;
bool                   svenClient     = false;
const char*            missingSymbol  = nullptr;
char                   error[1024]    = {};
std::vector<uintptr_t> removed;
RenderViewFn           hookedRenderView      = nullptr;
PaintBackgroundFn      hookedPaintBackground = nullptr;
RenderPortalsFn        hookedRenderPortals   = nullptr;
ResetAllFn             hookedResetAll        = nullptr;

int         clientState  = 1;
int         clientSignon = 0;
model_t*    worldModel   = nullptr;
cl_entity_t worldEntity  = {};

// Engine draw globals and what the fake draw functions observed.
constexpr int FxBlend       = 51;
cl_entity_t*  currentEntity = nullptr;
float         blend         = 0.25f;
float         entOrigin[3]  = {};
struct DrawCall
{
    const char*  function;
    cl_entity_t* current;
    float        blend;
    float        origin[3];
    int          flags;
    void*        player;
};
std::vector<DrawCall> drawCalls;

void RecordDraw(const char* function, int flags = 0, void* player = nullptr)
{
    DrawCall call{function, currentEntity, blend, {entOrigin[0], entOrigin[1], entOrigin[2]}, flags, player};
    drawCalls.push_back(call);
}

void Check(const char* label, int expected, int actual)
{
    if (expected != actual)
    {
        std::printf("FAIL %s: expected %d, got %d\n", label, expected, actual);
        ++failures;
    }
}

int portalManager = 0;
// When set, the fake V_RenderView draws the client triangles like the engine.
cl_exportfuncs_t* engineExports = nullptr;

// Sven's V_RenderView renders the client portals before the client V_CalcRefdef; the
// client resets its portal manager when it creates it on that first use.
void __cdecl OriginalRenderView()
{
    if (svenClient && hookedRenderPortals && hookedResetAll)
    {
        hookedResetAll(&portalManager, 0);
        ref_params_t refdef = {};
        hookedRenderPortals(&portalManager, 0, &refdef);
    }
    if (engineExports)
        engineExports->HUD_DrawNormalTriangles();
    ++mainRenders;
}
void __fastcall OriginalRenderPortals(void*, int, ref_params_s*) { ++portalRenders; }
int __fastcall  OriginalResetAll(void*, int)
{
    ++portalResets;
    return 1;
}
void __cdecl EngineDrawBrush(cl_entity_t*) { RecordDraw("brush"); }
void __cdecl EngineDrawSprite(cl_entity_t*) { RecordDraw("sprite"); }
int __cdecl  EngineFxBlend(cl_entity_t*) { return FxBlend; }

void __cdecl OriginalPaintBackground(int*)
{
    ++paints;
    hookedRenderView();
}

PVOID           EngineBase() { return reinterpret_cast<void*>(0x10000000); }
PVOID           ClientBase() { return reinterpret_cast<void*>(0x20000000); }
IBaseInterface* ClientInterface(const char* name, int*)
{
    static IBaseInterface svenClientInterface;
    return svenClient && std::strcmp(name, "SCClientDLL001") == 0 ? &svenClientInterface : nullptr;
}
CreateInterfaceFn      ClientFactory() { return ClientInterface; }
DWORD                  EngineBuild() { return 10257; }
mh_gamesymbol_status_t CRC64(PVOID, uint64_t* value)
{
    *value = 0x1234;
    return MH_GAMESYMBOL_OK;
}
const char* Status(mh_gamesymbol_status_t) { return "SYMBOL_NOT_FOUND"; }
void        SysError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(error, sizeof(error), format, args);
    va_end(args);
}

struct ExpectedSymbol
{
    const char*          name;
    mh_gamesymbol_kind_t kind;
    void*                address;
};

const ExpectedSymbol symbols[] = {
    {"V_RenderView", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(OriginalRenderView)},
    {"VGui_ViewportPaintBackground", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(OriginalPaintBackground)},
    {"cls.state", MH_GAMESYMBOL_KIND_GLOBAL, &clientState},
    {"cls.signon", MH_GAMESYMBOL_KIND_GLOBAL, &clientSignon},
    {"cl_worldmodel", MH_GAMESYMBOL_KIND_GLOBAL, &worldModel},
    {"r_worldentity", MH_GAMESYMBOL_KIND_GLOBAL, &worldEntity},
    {"R_DrawBrushModel", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(EngineDrawBrush)},
    {"R_DrawSpriteModel", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(EngineDrawSprite)},
    {"CL_FxBlend", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(EngineFxBlend)},
    {"currententity", MH_GAMESYMBOL_KIND_GLOBAL, &currentEntity},
    {"r_blend", MH_GAMESYMBOL_KIND_GLOBAL, &blend},
    {"r_entorigin", MH_GAMESYMBOL_KIND_GLOBAL, entOrigin},
};
constexpr int EngineSymbolCount = sizeof(symbols) / sizeof(symbols[0]);

const ExpectedSymbol clientSymbols[] = {
    {"ClientPortalManager_RenderPortals", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(OriginalRenderPortals)},
    {"ClientPortalManager_ResetAll", MH_GAMESYMBOL_KIND_FUNCTION, reinterpret_cast<void*>(OriginalResetAll)},
};

bool IsClientAddress(void* address)
{
    for (const auto& symbol : clientSymbols)
    {
        if (address == symbol.address)
            return true;
    }
    return false;
}

mh_gamesymbol_status_t Resolve(PVOID base, const char* symbol, mh_gamesymbol_kind_t kind, PVOID* address)
{
    ++resolves;
    if (missingSymbol && std::strcmp(symbol, missingSymbol) == 0)
        return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    for (const auto& expected : clientSymbols)
    {
        if (std::strcmp(symbol, expected.name) == 0)
        {
            Check("resolve real client base", 1, base == ClientBase());
            Check(expected.name, expected.kind, kind);
            *address = expected.address;
            return MH_GAMESYMBOL_OK;
        }
    }
    ++engineResolves;
    Check("resolve real engine base", 1, base == EngineBase());
    for (const auto& expected : symbols)
    {
        if (std::strcmp(symbol, expected.name) == 0)
        {
            Check(expected.name, expected.kind, kind);
            *address = expected.address;
            return MH_GAMESYMBOL_OK;
        }
    }
    Check("unexpected symbol", 0, 1);
    return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
}

// Inside LoadEngine MetaHook defers inline hooks: the trampoline is written on commit.
std::vector<std::pair<void**, void*>> pendingOriginals;

void CommitHooks()
{
    for (const auto& pending : pendingOriginals)
        *pending.first = pending.second;
    pendingOriginals.clear();
}

hook_t* InlineHook(void* address, void* replacement, void** original)
{
    ++installs;
    if (!IsClientAddress(address))
        Check("resolve every engine symbol before any engine installation", EngineSymbolCount, engineResolves);
    else
        Check("resolve every client symbol before any client installation", 2, resolves - engineResolves);
    if (failInstall == installs)
        return nullptr;
    pendingOriginals.emplace_back(original, address);
    if (address == clientSymbols[0].address)
        hookedRenderPortals = reinterpret_cast<RenderPortalsFn>(replacement);
    else if (address == clientSymbols[1].address)
        hookedResetAll = reinterpret_cast<ResetAllFn>(replacement);
    else if (address == symbols[1].address)
        hookedPaintBackground = reinterpret_cast<PaintBackgroundFn>(replacement);
    else
        hookedRenderView = reinterpret_cast<RenderViewFn>(replacement);
    return reinterpret_cast<hook_t*>(static_cast<uintptr_t>(installs));
}

BOOL UnHook(hook_t* handle)
{
    removed.push_back(reinterpret_cast<uintptr_t>(handle));
    return TRUE;
}

class CountingView : public IStandaloneView
{
public:
    bool IsActive() const override { return true; }
    void CalcRefdef(ref_params_s*) override { ++calcs; }
    void OnBeforeRenderView() override
    {
        ++renders;
        if (unregisterInBefore)
            StandaloneViewManager().UnregisterStandaloneView(this);
    }
    void OnAfterRenderView() override {}
    void DrawNormalTriangles(IStandaloneViewDrawfuncs*) override { ++draws; }
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs*) override { ++draws; }

    int renders = 0;
    int calcs   = 0;
    int draws   = 0;

    bool unregisterInBefore = false;
};

int  clientCalcs = 0;
int  clientDraws = 0;
void ClientCalcRefdef(ref_params_s*) { ++clientCalcs; }
void ClientDraw() { ++clientDraws; }

void Reset()
{
    standaloneview::UninstallEngineHooks();
    standaloneview::UninstallClientHooks();
    StandaloneViewManager().ClearViews();
    installs = resolves = engineResolves = failInstall = mainRenders = paints = portalRenders = portalResets = 0;
    svenClient                                                                                               = false;
    missingSymbol                                                                                            = nullptr;
    error[0]                                                                                                 = '\0';
    removed.clear();
    pendingOriginals.clear();
    hookedRenderView      = nullptr;
    hookedPaintBackground = nullptr;
    hookedRenderPortals   = nullptr;
    hookedResetAll        = nullptr;
    clientCalcs = clientDraws = 0;
}

void Success()
{
    Reset();
    Check("install succeeds", 1, standaloneview::InstallEngineHooks());
    Check("install two hooks", 2, installs);
    Check("repeated install succeeds", 1, standaloneview::InstallEngineHooks());
    Check("repeated install adds no hooks", 2, installs);
    CommitHooks();

    CountingView view;
    StandaloneViewManager().RegisterStandaloneView(&view);
    hookedRenderView();
    Check("V_RenderView outside paint background renders the main view", 1, mainRenders);
    Check("V_RenderView outside paint background skips views", 0, view.renders);

    int extents[4] = {};
    hookedPaintBackground(extents);
    Check("paint background calls the original", 1, paints);
    Check("paint background renders the view", 1, view.renders);
    Check("view and main view both reach the engine", 3, mainRenders);
    Check("cls.state restored", 1, clientState);

    error[0]                = '\0';
    view.unregisterInBefore = true;
    hookedPaintBackground(extents);
    Check("unregistering from a callback reaches MetaHook SysError", 1,
          std::strstr(error, "UnregisterStandaloneView") != nullptr);
    view.unregisterInBefore = false;
    StandaloneViewManager().UnregisterStandaloneView(&view);

    standaloneview::UninstallEngineHooks();
    Check("both removed", 2, static_cast<int>(removed.size()));
    if (removed.size() == 2)
    {
        Check("paint background removed first", 2, static_cast<int>(removed[0]));
        Check("V_RenderView removed second", 1, static_cast<int>(removed[1]));
    }
    standaloneview::UninstallEngineHooks();
    Check("uninstall is idempotent", 2, static_cast<int>(removed.size()));
}

cl_exportfuncs_t ClientExportTable()
{
    cl_exportfuncs_t exports             = {};
    exports.V_CalcRefdef                 = ClientCalcRefdef;
    exports.HUD_DrawNormalTriangles      = ClientDraw;
    exports.HUD_DrawTransparentTriangles = ClientDraw;
    return exports;
}

void ClientExports()
{
    Reset();
    auto exports = ClientExportTable();
    Check("client install succeeds", 1, standaloneview::InstallClientHooks(&exports));
    Check("non-Sven client installs no inline hook", 0, installs);
    Check("V_CalcRefdef replaced", 1, exports.V_CalcRefdef != ClientCalcRefdef);
    Check("HUD_DrawNormalTriangles replaced", 1, exports.HUD_DrawNormalTriangles != ClientDraw);
    Check("HUD_DrawTransparentTriangles replaced", 1, exports.HUD_DrawTransparentTriangles != ClientDraw);

    ref_params_t refdef = {};
    exports.V_CalcRefdef(&refdef);
    exports.HUD_DrawNormalTriangles();
    exports.HUD_DrawTransparentTriangles();
    Check("V_CalcRefdef forwards to the client", 1, clientCalcs);
    Check("triangle exports forward to the client", 2, clientDraws);

    standaloneview::UninstallClientHooks();
    exports.V_CalcRefdef(&refdef);
    Check("uninstalled client hooks stop forwarding", 1, clientCalcs);
}

void SvenPortals()
{
    Reset();
    svenClient   = true;
    auto exports = ClientExportTable();
    Check("Sven client install succeeds", 1, standaloneview::InstallClientHooks(&exports));
    Check("Sven client hooks RenderPortals and ResetAll", 2, installs);
    Check("repeated Sven client install succeeds", 1, standaloneview::InstallClientHooks(&exports));
    Check("repeated Sven client install adds no hook", 2, installs);
    Check("engine install succeeds", 1, standaloneview::InstallEngineHooks());
    CommitHooks();

    ref_params_t refdef = {};
    hookedRenderPortals(&portalManager, 0, &refdef);
    Check("RenderPortals forwards outside a view pass", 1, portalRenders);
    Check("ResetAll forwards outside a view pass", 1, hookedResetAll(&portalManager, 0));
    Check("ResetAll reaches the client outside a view pass", 1, portalResets);

    CountingView view;
    StandaloneViewManager().RegisterStandaloneView(&view);
    int extents[4] = {};
    portalRenders  = 0;
    portalResets   = 0;
    hookedPaintBackground(extents);
    Check("view renders", 1, view.renders);
    Check("view pass and main view reach the engine", 2, mainRenders);
    Check("portals render only for the main view", 1, portalRenders);
    Check("portal manager resets only for the main view", 1, portalResets);

    const auto engineHooks = static_cast<int>(removed.size());
    standaloneview::UninstallClientHooks();
    Check("portal hooks removed", engineHooks + 2, static_cast<int>(removed.size()));
    standaloneview::UninstallClientHooks();
    Check("client uninstall is idempotent", engineHooks + 2, static_cast<int>(removed.size()));
    standaloneview::UninstallEngineHooks();
}

void ClientFailures()
{
    for (const auto& symbol : clientSymbols)
    {
        Reset();
        svenClient    = true;
        missingSymbol = symbol.name;
        auto exports  = ClientExportTable();
        Check("missing client symbol fails", 0, standaloneview::InstallClientHooks(&exports));
        Check("missing client symbol installs nothing", 0, installs);
        Check("missing client symbol leaves exports", 1, exports.V_CalcRefdef == ClientCalcRefdef);
        Check("error names client symbol", 1, std::strstr(error, symbol.name) != nullptr);
        Check("error names client module", 1, std::strstr(error, "module client") != nullptr);
    }
    for (int failed = 1; failed <= 2; ++failed)
    {
        Reset();
        svenClient   = true;
        failInstall  = failed;
        auto exports = ClientExportTable();
        Check("client hook failure fails", 0, standaloneview::InstallClientHooks(&exports));
        Check("client hook failure rolls back prior hooks", failed - 1, static_cast<int>(removed.size()));
        Check("client hook failure leaves exports", 1, exports.V_CalcRefdef == ClientCalcRefdef);
        Check("client hook failure diagnostic", 1, std::strstr(error, "InlineHook failed") != nullptr);
    }
    Reset();
    svenClient   = true;
    auto exports = ClientExportTable();
    Check("client install can retry after failure", 1, standaloneview::InstallClientHooks(&exports));
}

int StudioDrawModel(int flags)
{
    RecordDraw("studio", flags);
    return 1;
}
int StudioDrawPlayer(int flags, entity_state_s* player)
{
    RecordDraw("player", flags, player);
    return 1;
}

constexpr int         MaxClients = 2;
entity_state_t        playerStates[MaxClients];
r_studio_interface_t  clientStudio          = {};
r_studio_interface_t* engineStudioInterface = nullptr; // the engine's pStudioAPI
engine_studio_api_t   engineStudioApi       = {};
cl_entity_t           attachmentOwner       = {};

entity_state_t* GetPlayerState(int index) { return &playerStates[index]; }
int             GetMaxClients() { return MaxClients; }
cl_entity_t*    GetEntityByIndex(int index) { return index == 7 ? &attachmentOwner : nullptr; }

int ClientGetStudioModelInterface(int, r_studio_interface_t** ppinterface, engine_studio_api_t*)
{
    *ppinterface = &clientStudio;
    return 1;
}

class DrawingView : public IStandaloneView
{
public:
    bool IsActive() const override { return true; }
    void CalcRefdef(ref_params_s*) override {}
    void OnBeforeRenderView() override {}
    void OnAfterRenderView() override {}
    void DrawNormalTriangles(IStandaloneViewDrawfuncs* pDrawfuncs) override
    {
        for (auto ent : brushes)
            pDrawfuncs->DrawBrushModel(ent);
        for (auto ent : sprites)
            pDrawfuncs->DrawSpriteModel(ent);
        for (auto ent : studios)
            pDrawfuncs->DrawStudioModel(ent);
    }
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs*) override {}

    std::vector<cl_entity_t*> brushes;
    std::vector<cl_entity_t*> sprites;
    std::vector<cl_entity_t*> studios;
};

void EntityDraws()
{
    Reset();
    Check("engine install succeeds", 1, standaloneview::InstallEngineHooks());
    auto exports                        = ClientExportTable();
    exports.HUD_GetStudioModelInterface = ClientGetStudioModelInterface;
    Check("client install succeeds", 1, standaloneview::InstallClientHooks(&exports));
    Check("HUD_GetStudioModelInterface replaced", 1, exports.HUD_GetStudioModelInterface != ClientGetStudioModelInterface);
    CommitHooks();
    // As the engine does in ClientDLL_CheckStudioInterface.
    Check("studio interface forwarded", 1, exports.HUD_GetStudioModelInterface(STUDIO_INTERFACE_VERSION, &engineStudioInterface, &engineStudioApi));
    Check("client studio interface reaches the engine", 1, engineStudioInterface == &clientStudio);

    model_t brushModel  = {};
    model_t spriteModel = {};
    model_t studioModel = {};
    brushModel.type     = mod_brush;
    spriteModel.type    = mod_sprite;
    studioModel.type    = mod_studio;

    cl_entity_t brush                = {};
    brush.model                      = &brushModel;
    cl_entity_t transSprite          = {};
    transSprite.model                = &spriteModel;
    transSprite.curstate.rendermode  = kRenderTransAdd;
    transSprite.curstate.skin        = 7;
    transSprite.curstate.body        = 2;
    transSprite.origin[0]            = 100;
    attachmentOwner.attachment[1][0] = 5;
    attachmentOwner.attachment[1][1] = 6;
    attachmentOwner.attachment[1][2] = 7;
    cl_entity_t studio               = {};
    studio.model                     = &studioModel;
    cl_entity_t player               = {};
    player.model                     = &studioModel;
    player.player                    = 1;
    player.index                     = 2;
    cl_entity_t invalidPlayer        = player;
    invalidPlayer.index              = MaxClients + 1;

    DrawingView view;
    view.brushes = {&brush};
    view.sprites = {&transSprite};
    view.studios = {&studio, &player, &invalidPlayer};
    StandaloneViewManager().RegisterStandaloneView(&view);

    cl_entity_t previous = {};
    currentEntity        = &previous;
    blend                = 0.25f;
    entOrigin[0]         = 9;
    int extents[4]       = {};
    engineExports        = &exports;
    hookedPaintBackground(extents);
    engineExports = nullptr;

    Check("valid entities draw once each", 4, static_cast<int>(drawCalls.size()));
    if (drawCalls.size() == 4)
    {
        Check("brush dispatches to R_DrawBrushModel", 0, std::strcmp(drawCalls[0].function, "brush"));
        Check("brush is the current entity", 1, drawCalls[0].current == &brush);
        Check("normal render mode draws opaque", 1, drawCalls[0].blend == 1.0f);

        Check("sprite dispatches to R_DrawSpriteModel", 0, std::strcmp(drawCalls[1].function, "sprite"));
        Check("sprite is the current entity", 1, drawCalls[1].current == &transSprite);
        Check("transparent render mode uses CL_FxBlend", 1, drawCalls[1].blend == FxBlend / 255.0f);
        Check("sprite follows its attachment", 1,
              drawCalls[1].origin[0] == 5 && drawCalls[1].origin[1] == 6 && drawCalls[1].origin[2] == 7);

        Check("studio dispatches to StudioDrawModel", 0, std::strcmp(drawCalls[2].function, "studio"));
        Check("studio is the current entity", 1, drawCalls[2].current == &studio);
        Check("studio renders without events", STUDIO_RENDER, drawCalls[2].flags);

        Check("player dispatches to StudioDrawPlayer", 0, std::strcmp(drawCalls[3].function, "player"));
        Check("player state of the entity's slot", 1, drawCalls[3].player == &playerStates[1]);
        Check("player renders without events", STUDIO_RENDER, drawCalls[3].flags);
    }
    Check("current entity restored", 1, currentEntity == &previous);
    Check("r_blend restored", 1, blend == 0.25f);
    Check("r_entorigin restored", 9, static_cast<int>(entOrigin[0]));
    Check("valid draws report nothing", 0, static_cast<int>(std::strlen(error)));

    StandaloneViewManager().UnregisterStandaloneView(&view);
    standaloneview::UninstallClientHooks();
    standaloneview::UninstallEngineHooks();
    drawCalls.clear();
}

void Failures()
{
    for (const auto& symbol : symbols)
    {
        Reset();
        missingSymbol = symbol.name;
        Check("missing symbol fails", 0, standaloneview::InstallEngineHooks());
        Check("missing symbol installs nothing", 0, installs);
        Check("error names symbol", 1, std::strstr(error, symbol.name) != nullptr);
        Check("error contains CRC64", 1, std::strstr(error, "0000000000001234") != nullptr);
        Check("error contains build", 1, std::strstr(error, "10257") != nullptr);
        Check("error contains status", 1, std::strstr(error, "SYMBOL_NOT_FOUND") != nullptr);
    }
    for (int failed = 1; failed <= 2; ++failed)
    {
        Reset();
        failInstall = failed;
        Check("hook failure fails installation", 0, standaloneview::InstallEngineHooks());
        Check("hook failure rolls back prior hooks", failed - 1, static_cast<int>(removed.size()));
        standaloneview::UninstallEngineHooks();
        Check("rollback leaves no hooks", failed - 1, static_cast<int>(removed.size()));
        Check("hook failure diagnostic", 1, std::strstr(error, "InlineHook failed") != nullptr);
    }
    Reset();
    Check("install can retry after failure", 1, standaloneview::InstallEngineHooks());
    standaloneview::UninstallEngineHooks();
}
} // namespace

int main()
{
    metahook_api_t api            = {};
    api.GetEngineBase             = EngineBase;
    api.GetClientBase             = ClientBase;
    api.GetClientFactory          = ClientFactory;
    api.GetEngineBuildnum         = EngineBuild;
    api.GetModuleCRC64            = CRC64;
    api.GetGameSymbolStatusString = Status;
    api.SysError                  = SysError;
    api.ResolveGameSymbol         = Resolve;
    api.InlineHook                = InlineHook;
    api.UnHook                    = UnHook;
    g_pMetaHookAPI                = &api;

    gEngfuncs.GetMaxClients        = GetMaxClients;
    gEngfuncs.GetEntityByIndex     = GetEntityByIndex;
    clientStudio.version           = STUDIO_INTERFACE_VERSION;
    clientStudio.StudioDrawModel   = StudioDrawModel;
    clientStudio.StudioDrawPlayer  = StudioDrawPlayer;
    engineStudioApi.GetPlayerState = GetPlayerState;
    Success();
    ClientExports();
    SvenPortals();
    ClientFailures();
    EntityDraws();
    Failures();
    std::printf("StandaloneView hooks: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
