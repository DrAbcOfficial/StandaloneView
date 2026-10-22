#pragma once

#include <IStandaloneView.h>
#include <vector>

struct cl_entity_s;
struct model_s;
struct ref_params_s;

namespace standaloneview
{
// cactive_t::ca_active and SIGNONS, the values V_RenderView requires before it renders.
constexpr int ClientStateActive = 5;
constexpr int SignonsComplete   = 2;

// Engine state used while rendering standalone views; resolved from gamedata by privatehook.cpp.
struct EngineBridge
{
    void (*V_RenderView)()    = nullptr;
    bool (*IsDemoPlayback)()  = nullptr;
    int*         clientState  = nullptr; // cls.state
    int*         clientSignon = nullptr; // cls.signon
    model_s**    worldModel   = nullptr; // cl.worldmodel
    cl_entity_s* worldEntity  = nullptr; // r_worldentity
    // Fatal error for consumer contract violations; MetaHook's SysError in the game.
    void (*SysError)(const char* fmt, ...) = nullptr;
    // Entity draws for IStandaloneViewDrawfuncs; they set and restore the engine's draw globals.
    void (*DrawBrushModel)(cl_entity_s* ent)  = nullptr;
    void (*DrawSpriteModel)(cl_entity_s* ent) = nullptr;
};

// The client exports replaced by StandaloneView, as they were before LoadClient.
struct ClientBridge
{
    void (*V_CalcRefdef)(ref_params_s* pparams) = nullptr;
    void (*DrawNormalTriangles)()               = nullptr;
    void (*DrawTransparentTriangles)()          = nullptr;
    // Draws through the studio model interface the engine received from the client.
    void (*DrawStudioModel)(cl_entity_s* ent) = nullptr;
};
} // namespace standaloneview

class CStandaloneViewManager : public IStandaloneViewManager
{
public:
    void RegisterStandaloneView(IStandaloneView* pView) override;
    void UnregisterStandaloneView(IStandaloneView* pView) override;

    void SetEngine(const standaloneview::EngineBridge& engine);
    void SetClient(const standaloneview::ClientBridge& client);
    void ResetEngine();
    void ResetClient();
    void ClearViews();

    // Hook bodies.
    void PaintBackground(void (*original)(int* extents), int* extents);
    void RenderView();
    void CalcRefdef(ref_params_s* pparams);
    void DrawNormalTriangles();
    void DrawTransparentTriangles();

    bool             IsRenderingStandaloneViews() const { return m_bRenderingStandaloneViews; }
    IStandaloneView* GetCurrentView() const { return m_pCurrentView; }

private:
    // Handed to the triangle callbacks of the view being rendered.
    class Drawfuncs final : public IStandaloneViewDrawfuncs
    {
    public:
        explicit Drawfuncs(CStandaloneViewManager& manager) :
            m_Manager(manager) {}

        void DrawBrushModel(cl_entity_s* ent) override;
        void DrawStudioModel(cl_entity_s* ent) override;
        void DrawSpriteModel(cl_entity_s* ent) override;

    private:
        CStandaloneViewManager& m_Manager;
    };

    void ReportMisuse(const char* function, const char* problem) const;
    bool CanDrawEntity(const cl_entity_s* ent, int modelType, const char* function) const;
    bool IsRegistered(const IStandaloneView* pView) const;
    bool ShouldRenderStandaloneViews() const;
    void RenderStandaloneViews();

    std::vector<IStandaloneView*> m_Views;
    standaloneview::EngineBridge  m_Engine{};
    standaloneview::ClientBridge  m_Client{};
    IStandaloneView*              m_pCurrentView              = nullptr;
    int                           m_iPaintBackgroundDepth     = 0;
    bool                          m_bRenderingStandaloneViews = false;
    bool                          m_bDrawingViewTriangles     = false;
    Drawfuncs                     m_Drawfuncs{*this};
};

CStandaloneViewManager& StandaloneViewManager();
