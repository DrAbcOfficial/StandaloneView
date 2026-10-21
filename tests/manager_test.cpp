#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <const.h>
#include <ref_params.h>
#include <cstdio>
#include <string>
#include <vector>

#include "EmptyWorld.h"
#include "StandaloneViewManager.h"

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
        if (renderInBefore)
            Manager().RenderView();
    }
    void OnAfterRenderView() override { Log(m_Name + ":after"); }
    void DrawNormalTriangles() override { Log(m_Name + ":normal"); }
    void DrawTransparentTriangles() override { Log(m_Name + ":trans"); }

    bool active             = true;
    bool unregisterInBefore = false;
    bool unregisterInCalc   = false;
    bool renderInBefore     = false;

private:
    std::string m_Name;
};

void ResetState()
{
    Manager().ClearViews();
    Manager().ResetEngine();
    Manager().ResetClient();

    standaloneview::EngineBridge engine;
    engine.V_RenderView   = EngineRenderView;
    engine.IsDemoPlayback = IsDemoPlayback;
    engine.clientState    = &clientState;
    engine.clientSignon   = &clientSignon;
    engine.worldModel     = &worldModel;
    engine.worldEntity    = &worldEntity;
    Manager().SetEngine(engine);

    standaloneview::ClientBridge client;
    client.V_CalcRefdef             = ClientCalcRefdef;
    client.DrawNormalTriangles      = ClientDrawNormal;
    client.DrawTransparentTriangles = ClientDrawTransparent;
    Manager().SetClient(client);

    events.clear();
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
    Manager().RegisterStandaloneView(&a);
    Manager().RegisterStandaloneView(&b);
    Frame();
    CheckText("view unregistered before rendering is skipped",
              "a:before(state=5,signon=2,empty) b:before(state=5,signon=2,empty) b:calc b:normal b:trans engine:view b:after main:skipped",
              events);

    ResetState();
    a.unregisterInBefore = false;
    a.unregisterInCalc   = true;
    Manager().RegisterStandaloneView(&a);
    Frame();
    CheckText("view unregistered while rendering receives no further callbacks",
              "a:before(state=5,signon=2,empty) a:calc engine:view main:skipped", events);
    Check("pass stays client-only", 1, lastRefdef.onlyClientDraw);
    Check("state restored after unregister", 1, clientState);
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
    ForwardingWithoutView();
    std::printf("StandaloneView manager: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
