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
};

// The client exports replaced by StandaloneView, as they were before LoadClient.
struct ClientBridge
{
    void (*V_CalcRefdef)(ref_params_s* pparams) = nullptr;
    void (*DrawNormalTriangles)()               = nullptr;
    void (*DrawTransparentTriangles)()          = nullptr;
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

    bool             IsRenderingStandaloneViews() const { return m_bRenderingViews; }
    IStandaloneView* GetCurrentView() const { return m_pCurrentView; }

private:
    bool IsRegistered(const IStandaloneView* pView) const;
    bool ShouldRenderStandaloneViews() const;
    void RenderStandaloneViews();

    std::vector<IStandaloneView*> m_Views;
    standaloneview::EngineBridge  m_Engine{};
    standaloneview::ClientBridge  m_Client{};
    IStandaloneView*              m_pCurrentView          = nullptr;
    int                           m_iPaintBackgroundDepth = 0;
    bool                          m_bRenderingViews       = false;
};

CStandaloneViewManager& StandaloneViewManager();
