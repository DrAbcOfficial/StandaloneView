#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <ref_params.h>
#include <algorithm>
#include <cmath>

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
    // The view list and the current view are in use until the views have rendered.
    if (m_bRenderingStandaloneViews)
    {
        ReportMisuse("UnregisterStandaloneView", "was called from a standalone view callback; unregister views outside them");
        return;
    }

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
    m_Client          = {};
    m_pStudioLighting = nullptr;
}

void CStandaloneViewManager::ClearViews()
{
    m_Views.clear();
}

void CStandaloneViewManager::ReportMisuse(const char* function, const char* problem) const
{
    if (m_Engine.SysError)
        m_Engine.SysError("[StandaloneView] %s %s.", function, problem);
}

bool CStandaloneViewManager::CanDrawEntity(const cl_entity_s* ent, int modelType, const char* function) const
{
    if (!m_bDrawingViewTriangles)
    {
        ReportMisuse(function, "was called outside the view's DrawNormalTriangles / DrawTransparentTriangles");
        return false;
    }
    if (!ent || !ent->model || ent->model->type != modelType)
    {
        ReportMisuse(function, "requires an entity whose model has the matching type");
        return false;
    }
    return true;
}

void CStandaloneViewManager::Drawfuncs::DrawBrushModel(cl_entity_s* ent)
{
    if (m_Manager.CanDrawEntity(ent, mod_brush, "IStandaloneViewDrawfuncs::DrawBrushModel") && m_Manager.m_Engine.DrawBrushModel)
        m_Manager.m_Engine.DrawBrushModel(ent);
}

void CStandaloneViewManager::Drawfuncs::DrawStudioModel(cl_entity_s* ent)
{
    if (m_Manager.CanDrawEntity(ent, mod_studio, "IStandaloneViewDrawfuncs::DrawStudioModel") && m_Manager.m_Client.DrawStudioModel)
        m_Manager.m_Client.DrawStudioModel(ent);
}

void CStandaloneViewManager::Drawfuncs::DrawSpriteModel(cl_entity_s* ent)
{
    if (m_Manager.CanDrawEntity(ent, mod_sprite, "IStandaloneViewDrawfuncs::DrawSpriteModel") && m_Manager.m_Engine.DrawSpriteModel)
        m_Manager.m_Engine.DrawSpriteModel(ent);
}

bool CStandaloneViewManager::Drawfuncs::SetupStudioPose(cl_entity_s* ent, cl_entity_s* boneSource)
{
    if (m_Manager.CanDrawEntity(ent, mod_studio, "IStandaloneViewDrawfuncsV003::SetupStudioPose"))
        return m_Manager.m_Client.SetupStudioPose != nullptr && m_Manager.m_Client.SetupStudioPose(ent, boneSource);
    return false;
}

void CStandaloneViewManager::Drawfuncs::DrawStudioModelEx(cl_entity_s* ent, cl_entity_s* boneSource, const standaloneview::StudioLighting* lighting)
{
    if (!m_Manager.CanDrawEntity(ent, mod_studio, "IStandaloneViewDrawfuncsV003::DrawStudioModelEx"))
        return;
    if (!m_Manager.m_Client.DrawStudioModelEx)
        return;

    // The lighting rides the draw: the client's studio renderer calls the engine's
    // StudioSetupLighting synchronously inside this scope, where the host's hook
    // reads it back through CurrentStudioLighting.
    const standaloneview::StudioLighting* pSavedLighting = m_Manager.m_pStudioLighting;
    m_Manager.m_pStudioLighting                          = lighting;
    m_Manager.m_Client.DrawStudioModelEx(ent, boneSource);
    m_Manager.m_pStudioLighting = pSavedLighting;
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
    if (m_iPaintBackgroundDepth <= 0 || m_bRenderingStandaloneViews || m_Views.empty())
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
    m_bRenderingStandaloneViews = true;

    {
        // Only an active client has a fully loaded world; check before forcing the state.
        const bool worldReady = *m_Engine.clientState == standaloneview::ClientStateActive &&
            *m_Engine.clientSignon == standaloneview::SignonsComplete;
        ClientStateScope                clientState(m_Engine.clientState, m_Engine.clientSignon);
        standaloneview::EmptyWorldScope emptyWorld(m_Engine.worldModel, m_Engine.worldEntity, worldReady);

        // Callbacks cannot unregister views; views they register start with the next frame.
        for (size_t i = 0, count = m_Views.size(); i < count; ++i)
        {
            const auto pView = m_Views[i];
            if (!pView->IsActive())
                continue;

            m_pCurrentView = pView;
            pView->OnBeforeRenderView();
            m_Engine.V_RenderView();
            pView->OnAfterRenderView();
            m_pCurrentView = nullptr;
        }
    }

    m_bRenderingStandaloneViews = false;
}

void CStandaloneViewManager::CalcRefdef(ref_params_s* pparams)
{
    if (m_pCurrentView)
    {
        // The client's V_CalcRefdef is skipped: its bob, punch and pitch drift state
        // advances per call and belongs to the main view only.
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
        m_bDrawingViewTriangles = true;
        m_pCurrentView->DrawNormalTriangles(&m_Drawfuncs);
        m_bDrawingViewTriangles = false;
        return;
    }

    if (m_Client.DrawNormalTriangles)
        m_Client.DrawNormalTriangles();
}

void CStandaloneViewManager::DrawTransparentTriangles()
{
    if (m_pCurrentView)
    {
        m_bDrawingViewTriangles = true;
        m_pCurrentView->DrawTransparentTriangles(&m_Drawfuncs);
        m_bDrawingViewTriangles = false;
        return;
    }

    if (m_Client.DrawTransparentTriangles)
        m_Client.DrawTransparentTriangles();
}

void CStandaloneViewManager::AdjustStudioLighting(struct alight_s* light, float* direction, const standaloneview::StudioLighting& lighting) const
{
    if (!light || !direction)
        return;

    const float flLength = std::sqrt(lighting.direction[0] * lighting.direction[0] +
                                     lighting.direction[1] * lighting.direction[1] +
                                     lighting.direction[2] * lighting.direction[2]);
    for (int i = 0; i < 3; ++i)
        direction[i] = flLength > 0.0f ? lighting.direction[i] / flLength : (i == 0 ? -1.0f : 0.0f);

    light->ambientlight = lighting.enabled ? lighting.ambientlight : 255;
    light->shadelight   = lighting.enabled ? lighting.shadelight : 0;
    for (int i = 0; i < 3; ++i)
        light->color[i] = lighting.enabled ? lighting.color[i] : 1.0f;
    light->plightvec = direction;
}
