#include <metahook.h>
#include <ref_params.h>
#include <algorithm>

#include "EmptyWorld.h"
#include "StandaloneViewManager.h"

namespace
{
CStandaloneViewManager g_StandaloneViewManager;

// Restores cls.state / cls.signon after V_RenderView was allowed to run.
class ClientStateScope
{
public:
    ClientStateScope(int* clientState, int* clientSignon) :
        m_pClientState(clientState),
        m_pClientSignon(clientSignon),
        m_iSavedState(*clientState),
        m_iSavedSignon(*clientSignon)
    {
        *m_pClientState  = standaloneview::ClientStateActive;
        *m_pClientSignon = standaloneview::SignonsComplete;
    }

    ~ClientStateScope()
    {
        *m_pClientState  = m_iSavedState;
        *m_pClientSignon = m_iSavedSignon;
    }

    ClientStateScope(const ClientStateScope&)            = delete;
    ClientStateScope& operator=(const ClientStateScope&) = delete;

private:
    int* m_pClientState;
    int* m_pClientSignon;
    int  m_iSavedState;
    int  m_iSavedSignon;
};
} // namespace

CStandaloneViewManager& StandaloneViewManager()
{
    return g_StandaloneViewManager;
}

void CStandaloneViewManager::RegisterStandaloneView(IStandaloneView* pView)
{
    if (!pView || IsRegistered(pView))
        return;

    m_Views.emplace_back(pView);
}

void CStandaloneViewManager::UnregisterStandaloneView(IStandaloneView* pView)
{
    m_Views.erase(std::remove(m_Views.begin(), m_Views.end(), pView), m_Views.end());
}

void CStandaloneViewManager::SetEngine(const standaloneview::EngineBridge& engine)
{
    m_Engine = engine;
}

void CStandaloneViewManager::SetClient(const standaloneview::ClientBridge& client)
{
    m_Client = client;
}

void CStandaloneViewManager::ResetEngine()
{
    m_Engine                = {};
    m_iPaintBackgroundDepth = 0;
}

void CStandaloneViewManager::ResetClient()
{
    m_Client = {};
}

void CStandaloneViewManager::ClearViews()
{
    m_Views.clear();
}

bool CStandaloneViewManager::IsRegistered(const IStandaloneView* pView) const
{
    return std::find(m_Views.begin(), m_Views.end(), pView) != m_Views.end();
}

void CStandaloneViewManager::PaintBackground(void (*original)(int* extents), int* extents)
{
    ++m_iPaintBackgroundDepth;
    original(extents);
    --m_iPaintBackgroundDepth;
}

bool CStandaloneViewManager::ShouldRenderStandaloneViews() const
{
    // Only the engine's own per-frame call renders views: other plugins may call
    // V_RenderView just to refresh the refdef (BulletPhysics does so from HUD_Frame).
    if (m_iPaintBackgroundDepth <= 0 || m_bRenderingViews || m_Views.empty())
        return false;

    // Demo playback overwrites the refdef with the recorded view after CalcRefdef.
    if (m_Engine.IsDemoPlayback && m_Engine.IsDemoPlayback())
        return false;

    return m_Engine.clientState && m_Engine.clientSignon && m_Engine.worldModel && m_Engine.worldEntity;
}

void CStandaloneViewManager::RenderView()
{
    if (!m_Engine.V_RenderView)
        return;

    // Views render before the main view, so the main view's pass leaves the listener
    // origin, the demo view info and the client view state it writes back as usual.
    if (ShouldRenderStandaloneViews())
        RenderStandaloneViews();

    m_Engine.V_RenderView();
}

void CStandaloneViewManager::RenderStandaloneViews()
{
    m_bRenderingViews = true;

    {
        // Only an active client has a fully loaded world; check before forcing the state.
        const bool worldReady = *m_Engine.clientState == standaloneview::ClientStateActive &&
            *m_Engine.clientSignon == standaloneview::SignonsComplete;
        ClientStateScope                clientState(m_Engine.clientState, m_Engine.clientSignon);
        standaloneview::EmptyWorldScope emptyWorld(m_Engine.worldModel, m_Engine.worldEntity, worldReady);

        // Callbacks may unregister views, including the one being rendered.
        const auto views = m_Views;

        for (auto pView : views)
        {
            if (!IsRegistered(pView) || !pView->IsActive())
                continue;

            m_pCurrentView = pView;

            pView->OnBeforeRenderView();

            if (IsRegistered(pView))
                m_Engine.V_RenderView();

            if (IsRegistered(pView))
                pView->OnAfterRenderView();

            m_pCurrentView = nullptr;
        }
    }

    m_bRenderingViews = false;
}

void CStandaloneViewManager::CalcRefdef(ref_params_s* pparams)
{
    if (m_pCurrentView)
    {
        // The client's V_CalcRefdef is skipped: its bob, punch and pitch drift state
        // advances per call and belongs to the main view only.
        if (IsRegistered(m_pCurrentView))
            m_pCurrentView->CalcRefdef(pparams);
        pparams->onlyClientDraw = 1;
        pparams->nextView       = 0;
        return;
    }

    if (m_Client.V_CalcRefdef)
        m_Client.V_CalcRefdef(pparams);
}

void CStandaloneViewManager::DrawNormalTriangles()
{
    if (m_pCurrentView)
    {
        if (IsRegistered(m_pCurrentView))
            m_pCurrentView->DrawNormalTriangles();
        return;
    }

    if (m_Client.DrawNormalTriangles)
        m_Client.DrawNormalTriangles();
}

void CStandaloneViewManager::DrawTransparentTriangles()
{
    if (m_pCurrentView)
    {
        if (IsRegistered(m_pCurrentView))
            m_pCurrentView->DrawTransparentTriangles();
        return;
    }

    if (m_Client.DrawTransparentTriangles)
        m_Client.DrawTransparentTriangles();
}
