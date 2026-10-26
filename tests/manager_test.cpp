#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <const.h>
#include <ref_params.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "EmptyWorld.h"
#include "StandaloneViewManager.h"

// EmptyWorld.cpp reads r_novis through the client funcs; the default table leaves
// pfnGetCvarPointer null, which the arming treats as "no cvar access".
cl_enginefunc_t gEngfuncs = {};

namespace
{
int failures = 0;

void Check(const char* label, int expected, int actual)
{
    if (expected != actual)
    {
        std::printf("FAIL %s: expected %d, got %d\n", label, expected, actual);
        ++failures;
    }
}

void CheckText(const char* label, const std::string& expected, const std::string& actual)
{
    if (expected != actual)
    {
        std::printf("FAIL %s:\n  expected %s\n  got      %s\n", label, expected.c_str(), actual.c_str());
        ++failures;
    }
}

std::string  events;
int          clientState  = 1; // ca_disconnected
int          clientSignon = 0;
model_t*     worldModel   = nullptr;
cl_entity_t  worldEntity  = {};
model_t      loadedWorld  = {};
bool         demoPlayback = false;
ref_params_t lastRefdef   = {};

void Log(const std::string& text)
{
    if (!events.empty())
        events += ' ';
    events += text;
}

std::string lastError;

// The real SysError does not return; the fake records the call and continues.
void SysError(const char* format, ...)
{
    char    message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Log("sys:error");
    lastError = message;
}

void EngineDrawBrush(cl_entity_s*) { Log("engine:brush"); }
void EngineDrawSprite(cl_entity_s*) { Log("engine:sprite"); }
void ClientDrawStudio(cl_entity_s*) { Log("client:studio"); }

CStandaloneViewManager& Manager()
{
    return StandaloneViewManager();
}

// Mirrors the engine: V_RenderView gates on cls, asks the client for the refdef and
// draws the client triangles of the resulting pass.
void EngineRenderView()
{
    if (clientState != standaloneview::ClientStateActive || clientSignon != standaloneview::SignonsComplete)
    {
        Log("main:skipped");
        return;
    }
    if (!worldModel || worldEntity.model != worldModel)
    {
        Log("engine:no-world");
        return;
    }
    ref_params_t refdef = {};
    Manager().CalcRefdef(&refdef);
    lastRefdef = refdef;
    Manager().DrawNormalTriangles();
    Manager().DrawTransparentTriangles();
    Log(Manager().GetCurrentView() ? "engine:view" : "engine:main");
}

bool IsDemoPlayback() { return demoPlayback; }

void ClientCalcRefdef(ref_params_s* pparams)
{
    pparams->vieworg[0] = 1;
    Log("client:calc");
}
void ClientDrawNormal() { Log("client:normal"); }
void ClientDrawTransparent() { Log("client:trans"); }

class TestView : public IStandaloneView
{
public:
    explicit TestView(const char* name) : m_Name(name) {}

    bool IsActive() const override { return active; }
    void CalcRefdef(ref_params_s* pparams) override
    {
        pparams->vieworg[0]     = 42;
        pparams->onlyClientDraw = 0;
        pparams->nextView       = 1;
        Log(m_Name + ":calc");
        if (unregisterInCalc)
            Manager().UnregisterStandaloneView(this);
    }
    void OnBeforeRenderView() override
    {
        Log(m_Name + ":before(state=" + std::to_string(clientState) + ",signon=" + std::to_string(clientSignon) +
            (worldModel == standaloneview::GetEmptyWorldModel() ? ",empty" : "") + ")");
        if (unregisterInBefore)
            Manager().UnregisterStandaloneView(this);
        if (registerInBefore)
            Manager().RegisterStandaloneView(registerInBefore);
        if (renderInBefore)
            Manager().RenderView();
    }
    void OnAfterRenderView() override { Log(m_Name + ":after"); }
    void DrawNormalTriangles(IStandaloneViewDrawfuncs* pDrawfuncs) override
    {
        Log(m_Name + ":normal");
        Draw(pDrawfuncs);
    }
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs* pDrawfuncs) override
    {
        Log(m_Name + ":trans");
        Draw(pDrawfuncs);
    }

    bool active             = true;
    bool unregisterInBefore = false;
    bool unregisterInCalc   = false;
    bool renderInBefore     = false;

    IStandaloneView* registerInBefore = nullptr;
    // Drawn from both triangle callbacks with the function named by the model type.
    std::vector<cl_entity_t*> drawEntities;
    // Uses the brush function regardless of the model type.
    bool                      drawAsBrush   = false;
    IStandaloneViewDrawfuncs* lastDrawfuncs = nullptr;

private:
    void Draw(IStandaloneViewDrawfuncs* pDrawfuncs)
    {
        lastDrawfuncs = pDrawfuncs;
        for (auto ent : drawEntities)
        {
            if (drawAsBrush)
            {
                pDrawfuncs->DrawBrushModel(ent);
                continue;
            }
            switch (ent->model->type)
            {
                case mod_brush: pDrawfuncs->DrawBrushModel(ent); break;
                case mod_studio: pDrawfuncs->DrawStudioModel(ent); break;
                case mod_sprite: pDrawfuncs->DrawSpriteModel(ent); break;
                default: break;
            }
        }
    }

private:
    std::string m_Name;
};

void ResetState()
{
    Manager().ClearViews();
    Manager().ResetEngine();
    Manager().ResetClient();

    standaloneview::EngineBridge engine;
    engine.V_RenderView    = EngineRenderView;
    engine.IsDemoPlayback  = IsDemoPlayback;
    engine.clientState     = &clientState;
    engine.clientSignon    = &clientSignon;
    engine.worldModel      = &worldModel;
    engine.worldEntity     = &worldEntity;
    engine.SysError        = SysError;
    engine.DrawBrushModel  = EngineDrawBrush;
    engine.DrawSpriteModel = EngineDrawSprite;
    Manager().SetEngine(engine);

    standaloneview::ClientBridge client;
    client.V_CalcRefdef             = ClientCalcRefdef;
    client.DrawNormalTriangles      = ClientDrawNormal;
    client.DrawTransparentTriangles = ClientDrawTransparent;
    client.DrawStudioModel          = ClientDrawStudio;
    Manager().SetClient(client);

    events.clear();
    lastError.clear();
    clientState  = 1;
    clientSignon = 0;
    worldModel   = nullptr;
    worldEntity  = {};
    demoPlayback = false;
    lastRefdef   = {};
}

// The engine calls V_RenderView from VGui_ViewportPaintBackground once per frame.
void Frame()
{
    Manager().PaintBackground([](int*) { Manager().RenderView(); }, nullptr);
}

void EnterMap()
{
    clientState       = standaloneview::ClientStateActive;
    clientSignon      = standaloneview::SignonsComplete;
    worldModel        = &loadedWorld;
    worldEntity.model = &loadedWorld;
}

void EmptyWorldShape()
{
    const model_t* world = standaloneview::GetEmptyWorldModel();
    Check("empty world is a brush model", mod_brush, world->type);
    Check("root node is leafs[0]", 1, reinterpret_cast<const void*>(world->nodes) == world->leafs);
    Check("root is an empty leaf", CONTENTS_EMPTY, world->leafs->contents);
    Check("no visible leafs", 0, world->numleafs);
    Check("no surfaces", 0, world->numsurfaces);
}

void Registration()
{
    ResetState();
    TestView view("a");
    Manager().RegisterStandaloneView(nullptr);
    Manager().RegisterStandaloneView(&view);
    Manager().RegisterStandaloneView(&view);
    Frame();
    CheckText("duplicate registration renders once", "a:before(state=5,signon=2,empty) a:calc a:normal a:trans engine:view a:after main:skipped", events);

    events.clear();
    Manager().UnregisterStandaloneView(&view);
    Manager().UnregisterStandaloneView(&view);
    Frame();
    CheckText("unregistered view is not rendered", "main:skipped", events);
}

void MainMenuFrame()
{
    ResetState();
    TestView a("a");
    TestView b("b");
    Manager().RegisterStandaloneView(&a);
    Manager().RegisterStandaloneView(&b);
    Frame();
    CheckText("views render before the main view with forced state and the empty world",
              "a:before(state=5,signon=2,empty) a:calc a:normal a:trans engine:view a:after "
              "b:before(state=5,signon=2,empty) b:calc b:normal b:trans engine:view b:after main:skipped",
              events);
    Check("cls.state restored", 1, clientState);
    Check("cls.signon restored", 0, clientSignon);
    Check("cl.worldmodel restored", 1, worldModel == nullptr);
    Check("r_worldentity.model restored", 1, worldEntity.model == nullptr);
    Check("view refdef applied", 42, static_cast<int>(lastRefdef.vieworg[0]));
    Check("onlyClientDraw forced", 1, lastRefdef.onlyClientDraw);
    Check("nextView forced", 0, lastRefdef.nextView);
}

void InGameFrame()
{
    ResetState();
    EnterMap();
    TestView view("a");
    Manager().RegisterStandaloneView(&view);
    Frame();
    CheckText("loaded world is kept and the main view uses the client",
              "a:before(state=5,signon=2) a:calc a:normal a:trans engine:view a:after "
              "client:calc client:normal client:trans engine:main",
              events);
    Check("loaded world untouched", 1, worldModel == &loadedWorld);
    Check("main view refdef from client", 1, static_cast<int>(lastRefdef.vieworg[0]));
    Check("main view keeps onlyClientDraw", 0, lastRefdef.onlyClientDraw);
}

// CL_RegisterResources sets cl.worldmodel and redraws the loading screen before R_NewMap
// sets r_worldentity.model; after R_NewMap the client is still not active.
void LoadingFrames()
{
    const model_t* entityModels[] = {nullptr, &loadedWorld};
    for (const auto* entityModel : entityModels)
    {
        ResetState();
        clientState       = 3; // ca_connected
        clientSignon      = 1;
        worldModel        = &loadedWorld;
        worldEntity.model = const_cast<model_t*>(entityModel);
        TestView view("a");
        Manager().RegisterStandaloneView(&view);
        Frame();
        CheckText("a loading world is replaced by the empty world",
                  "a:before(state=5,signon=2,empty) a:calc a:normal a:trans engine:view a:after main:skipped", events);
        Check("loading cl.worldmodel restored", 1, worldModel == &loadedWorld);
        Check("loading r_worldentity.model restored", 1, worldEntity.model == entityModel);
    }

    ResetState();
    EnterMap();
    worldEntity.model = nullptr;
    TestView view("a");
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("an active client without r_worldentity.model gets the empty world", 1,
          events.rfind("a:before(state=5,signon=2,empty)", 0) == 0);
    Check("active cl.worldmodel restored", 1, worldModel == &loadedWorld);
    Check("active r_worldentity.model restored", 1, worldEntity.model == nullptr);
}

void Gates()
{
    ResetState();
    EnterMap();
    TestView view("a");
    Manager().RegisterStandaloneView(&view);

    Manager().RenderView();
    CheckText("outside paint background only the main view renders", "client:calc client:normal client:trans engine:main", events);

    events.clear();
    view.active = false;
    Frame();
    CheckText("inactive view is skipped", "client:calc client:normal client:trans engine:main", events);

    events.clear();
    view.active  = true;
    demoPlayback = true;
    Frame();
    CheckText("demo playback skips views", "client:calc client:normal client:trans engine:main", events);

    events.clear();
    demoPlayback        = false;
    view.renderInBefore = true;
    Frame();
    CheckText("nested V_RenderView does not re-enter the view loop",
              "a:before(state=5,signon=2) a:calc a:normal a:trans engine:view a:calc a:normal a:trans engine:view a:after "
              "client:calc client:normal client:trans engine:main",
              events);

    Manager().ResetEngine();
    events.clear();
    Manager().RenderView();
    Frame();
    CheckText("missing engine bridge does nothing", "", events);
}

void UnregisterDuringRender()
{
    ResetState();
    TestView a("a");
    TestView b("b");
    a.unregisterInBefore = true;
    b.unregisterInCalc   = true;
    Manager().RegisterStandaloneView(&a);
    Manager().RegisterStandaloneView(&b);
    Frame();
    CheckText("unregistering from a callback is a fatal error and keeps the view",
              "a:before(state=5,signon=2,empty) sys:error a:calc a:normal a:trans engine:view a:after "
              "b:before(state=5,signon=2,empty) b:calc sys:error b:normal b:trans engine:view b:after main:skipped",
              events);
    Check("error names the call", 1, lastError.find("UnregisterStandaloneView") != std::string::npos);
    Check("pass stays client-only", 1, lastRefdef.onlyClientDraw);
    Check("state restored after the violation", 1, clientState);

    events.clear();
    a.unregisterInBefore = false;
    b.unregisterInCalc   = false;
    Manager().UnregisterStandaloneView(&a);
    Manager().UnregisterStandaloneView(&b);
    Frame();
    CheckText("unregistering outside callbacks removes the views", "main:skipped", events);
}

void RegisterDuringRender()
{
    ResetState();
    TestView a("a");
    TestView b("b");
    a.registerInBefore = &b;
    Manager().RegisterStandaloneView(&a);
    Frame();
    CheckText("a view registered from a callback waits for the next frame",
              "a:before(state=5,signon=2,empty) a:calc a:normal a:trans engine:view a:after main:skipped", events);

    events.clear();
    a.registerInBefore = nullptr;
    Frame();
    CheckText("a view registered from a callback renders from the next frame",
              "a:before(state=5,signon=2,empty) a:calc a:normal a:trans engine:view a:after "
              "b:before(state=5,signon=2,empty) b:calc b:normal b:trans engine:view b:after main:skipped",
              events);
    Check("registering from a callback is allowed", 1, lastError.empty());
}

void Drawfuncs()
{
    model_t brushModel  = {};
    model_t studioModel = {};
    model_t spriteModel = {};
    brushModel.type     = mod_brush;
    studioModel.type    = mod_studio;
    spriteModel.type    = mod_sprite;
    cl_entity_t brush   = {};
    cl_entity_t studio  = {};
    cl_entity_t sprite  = {};
    brush.model         = &brushModel;
    studio.model        = &studioModel;
    sprite.model        = &spriteModel;

    ResetState();
    TestView view("a");
    view.drawEntities = {&brush, &studio, &sprite};
    Manager().RegisterStandaloneView(&view);
    Frame();
    CheckText("triangle callbacks draw entities through the renderer",
              "a:before(state=5,signon=2,empty) a:calc a:normal engine:brush client:studio engine:sprite "
              "a:trans engine:brush client:studio engine:sprite engine:view a:after main:skipped",
              events);
    Check("matching draws are valid", 1, lastError.empty());

    events.clear();
    view.drawEntities = {};
    view.lastDrawfuncs->DrawStudioModel(&studio);
    CheckText("draw functions outside the triangle callbacks are rejected", "sys:error", events);
    Check("error names the misused function", 1,
          lastError.find("IStandaloneViewDrawfuncs::DrawStudioModel was called outside") != std::string::npos);

    ResetState();
    view.drawEntities = {&studio};
    view.drawAsBrush  = true;
    Manager().RegisterStandaloneView(&view);
    Frame();
    CheckText("a model of the wrong type is rejected",
              "a:before(state=5,signon=2,empty) a:calc a:normal sys:error a:trans sys:error engine:view a:after main:skipped",
              events);
    Check("error names the type mismatch", 1, lastError.find("matching type") != std::string::npos);

    ResetState();
    cl_entity_t empty = {};
    view.drawEntities = {&empty};
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("an entity without a model is rejected", 1, lastError.find("matching type") != std::string::npos);
}

void ForwardingWithoutView()
{
    ResetState();
    ref_params_t refdef = {};
    Manager().CalcRefdef(&refdef);
    Manager().DrawNormalTriangles();
    Manager().DrawTransparentTriangles();
    CheckText("exports forward without a current view", "client:calc client:normal client:trans", events);

    Manager().ResetClient();
    events.clear();
    Manager().CalcRefdef(&refdef);
    Manager().DrawNormalTriangles();
    Manager().DrawTransparentTriangles();
    CheckText("missing client exports are ignored", "", events);
}

cl_entity_t* lastPoseEnt            = nullptr;
cl_entity_t* lastPoseBoneSource     = nullptr;
bool         poseResult             = true;
cl_entity_t* lastDrawExEnt          = nullptr;
cl_entity_t* lastDrawExBoneSource   = nullptr;
const standaloneview::StudioLighting* drawExActiveLighting = nullptr;
standaloneview::StudioLighting previewLighting{};

bool ClientSetupStudioPose(cl_entity_s* ent, cl_entity_s* boneSource)
{
    lastPoseEnt        = ent;
    lastPoseBoneSource = boneSource;
    return poseResult;
}

void ClientDrawStudioModelEx(cl_entity_s* ent, cl_entity_s* boneSource)
{
    lastDrawExEnt        = ent;
    lastDrawExBoneSource = boneSource;
    drawExActiveLighting = Manager().CurrentStudioLighting();
}

// Calls the API_003 studio extensions from the normal triangle callback; the drawfuncs
// pointer is only valid until the callback returns.
class V003View : public IStandaloneView
{
public:
    bool IsActive() const override { return true; }
    void CalcRefdef(ref_params_s*) override {}
    void OnBeforeRenderView() override {}
    void OnAfterRenderView() override {}
    void DrawNormalTriangles(IStandaloneViewDrawfuncs* pDrawfuncs) override { Draw(pDrawfuncs); }
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs*) override {}

    void Draw(IStandaloneViewDrawfuncs* pDrawfuncs)
    {
        auto*         v003 = static_cast<IStandaloneViewDrawfuncsV003*>(pDrawfuncs);
        lastDrawfuncs      = v003;
        if (callPose)
            posed = v003->SetupStudioPose(studio, useBoneSource ? source : nullptr);
        if (callDrawEx)
            v003->DrawStudioModelEx(studio, useBoneSource ? source : nullptr,
                                    useLighting ? &previewLighting : nullptr);
    }

    cl_entity_t*                  studio        = nullptr;
    cl_entity_t*                  source        = nullptr;
    bool                          callPose      = false;
    bool                          callDrawEx    = false;
    bool                          useBoneSource = false;
    bool                          useLighting   = false;
    bool                          posed         = false;
    IStandaloneViewDrawfuncsV003* lastDrawfuncs = nullptr;
};

void V003StudioDrawfuncs()
{
    model_t studioModel = {};
    model_t brushModel  = {};
    studioModel.type    = mod_studio;
    brushModel.type     = mod_brush;
    cl_entity_t studio = {};
    cl_entity_t source = {};
    cl_entity_t brush  = {};
    studio.model       = &studioModel;
    source.model       = &studioModel;
    brush.model        = &brushModel;

    ResetState();
    standaloneview::ClientBridge client;
    client.DrawStudioModel   = ClientDrawStudio;
    client.SetupStudioPose   = ClientSetupStudioPose;
    client.DrawStudioModelEx = ClientDrawStudioModelEx;
    Manager().SetClient(client);

    V003View view;
    view.studio = &studio;
    view.source = &source;
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("a frame without V003 calls is clean", 1, lastError.empty());

    view.callPose = true;
    view.lastDrawfuncs->SetupStudioPose(&studio, nullptr);
    Check("V003 pose outside the callbacks is rejected", 1,
          lastError.find("IStandaloneViewDrawfuncsV003::SetupStudioPose was called outside") != std::string::npos);

    events.clear();
    lastError.clear();
    view.lastDrawfuncs->DrawStudioModelEx(&studio, nullptr, nullptr);
    Check("V003 draw outside the callbacks is rejected", 1,
          lastError.find("IStandaloneViewDrawfuncsV003::DrawStudioModelEx was called outside") != std::string::npos);

    events.clear();
    lastError.clear();
    view.callDrawEx     = true;
    view.useBoneSource  = true;
    view.useLighting    = true;
    Frame();
    Check("the pose reaches the bridge with the bone source", 1, lastPoseEnt == &studio && lastPoseBoneSource == &source);
    Check("the pose result is forwarded", 1, view.posed);
    Check("the draw reaches the bridge with the bone source", 1, lastDrawExEnt == &studio && lastDrawExBoneSource == &source);
    Check("the configured lighting is active during the draw", 1, drawExActiveLighting == &previewLighting);
    Check("the lighting scope ends with the draw", 1, Manager().CurrentStudioLighting() == nullptr);
    Check("no misuse during the pass", 1, lastError.empty());

    events.clear();
    view.useLighting = false;
    Frame();
    Check("null lighting keeps the engine lighting", 1, drawExActiveLighting == nullptr);

    events.clear();
    lastError.clear();
    view.studio = &brush;
    Frame();
    Check("a model of the wrong type is rejected", 1, lastError.find("matching type") != std::string::npos);

    ResetState();
    view.studio = &studio;
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("missing V003 bridges pose nothing", 0, view.posed);
    Check("missing V003 bridges draw nothing", 1, lastDrawExEnt == nullptr);
    Check("missing V003 bridges are not misuse", 1, lastError.empty());
}

void AdjustStudioLightingMath()
{
    standaloneview::StudioLighting lighting{};
    lighting.direction[1] = 2.0f;
    alight_t light{};
    light.ambientlight = 10;
    light.shadelight   = 20;
    light.color[0]     = 0.25f;
    float direction[3] = {};

    Manager().AdjustStudioLighting(&light, direction, lighting);
    Check("direction is normalized", 1, direction[0] == 0.0f && direction[1] == 1.0f && direction[2] == 0.0f);
    Check("ambient and shade applied", 1, light.ambientlight == 96 && light.shadelight == 96);
    Check("color applied", 1, light.color[0] == 0.5f);
    Check("plightvec points at the direction", 1, light.plightvec == direction);

    lighting.direction[0] = 0.0f;
    lighting.direction[1] = 0.0f;
    lighting.direction[2] = 0.0f;
    Manager().AdjustStudioLighting(&light, direction, lighting);
    Check("a zero direction falls back along x", 1, direction[0] == -1.0f && direction[1] == 0.0f);

    lighting.enabled = false;
    Manager().AdjustStudioLighting(&light, direction, lighting);
    Check("disabled lighting draws full ambient", 1,
          light.ambientlight == 255 && light.shadelight == 0 && light.color[0] == 1.0f);
}

cvar_t noVisCvar = {};

cvar_t* FakeGetCvarPointer(const char*)
{
    return &noVisCvar;
}

class NoVisProbe : public IStandaloneView
{
public:
    bool IsActive() const override { return true; }
    void CalcRefdef(ref_params_s*) override {}
    void OnBeforeRenderView() override { valueDuringPass = noVisCvar.value; }
    void OnAfterRenderView() override {}
    void DrawNormalTriangles(IStandaloneViewDrawfuncs*) override {}
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs*) override {}

    float valueDuringPass = 0.0f;
};

void EmptyWorldNoVis()
{
    ResetState();
    noVisCvar.value             = 0.0f;
    gEngfuncs.pfnGetCvarPointer = FakeGetCvarPointer;
    NoVisProbe view;
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("the empty world pass rides r_novis", 1, static_cast<int>(view.valueDuringPass));
    Check("r_novis restored after the pass", 0, static_cast<int>(noVisCvar.value));

    ResetState();
    noVisCvar.value = 0.0f;
    EnterMap();
    Manager().RegisterStandaloneView(&view);
    Frame();
    Check("a loaded world keeps r_novis", 0, static_cast<int>(view.valueDuringPass));
    Check("loaded-world r_novis untouched", 0, static_cast<int>(noVisCvar.value));

    gEngfuncs.pfnGetCvarPointer = nullptr;
}
} // namespace

int main()
{
    EmptyWorldShape();
    Registration();
    MainMenuFrame();
    InGameFrame();
    LoadingFrames();
    Gates();
    UnregisterDuringRender();
    RegisterDuringRender();
    Drawfuncs();
    ForwardingWithoutView();
    V003StudioDrawfuncs();
    AdjustStudioLightingMath();
    EmptyWorldNoVis();
    std::printf("StandaloneView manager: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
