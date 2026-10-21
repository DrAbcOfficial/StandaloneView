#pragma once

#ifndef STANDALONE_VIEW_INTERFACE_H
#    define STANDALONE_VIEW_INTERFACE_H

#    include <interface.h>

struct ref_params_s;

/*
    A view rendered by StandaloneView before the engine's main view, once per frame.

    Every callback runs on the main thread inside the engine's V_RenderView, while
    cls.state / cls.signon are forced active. Until a map is fully loaded,
    cl.worldmodel is temporarily an empty brush model owned by StandaloneView, and
    HUD sprites (SPR_Load) are unavailable before the first map load.
*/
class IStandaloneView
{
public:
    // Return false to skip rendering for the current frame.
    virtual bool IsActive() const = 0;

    // Fill vieworg, viewangles and viewport (x, y, width, height in window pixels).
    // Do not change the cl_* fields: the engine writes them back into client state.
    // onlyClientDraw and nextView are forced afterwards.
    virtual void CalcRefdef(struct ref_params_s* pparams) = 0;

    virtual void OnBeforeRenderView() = 0;

    // The view has been drawn into the viewport of the currently bound framebuffer.
    // Copy it out here. StandaloneView does not clear the framebuffer afterwards:
    // clear whatever the main view would otherwise see.
    virtual void OnAfterRenderView() = 0;

    // Replace the client's HUD_DrawNormalTriangles / HUD_DrawTransparentTriangles
    // while this view is rendered. The engine's 3D matrices are already set up.
    virtual void DrawNormalTriangles()      = 0;
    virtual void DrawTransparentTriangles() = 0;
};

class IStandaloneViewManager : public IBaseInterface
{
public:
    // Registering twice is ignored. A view may unregister itself from its own callbacks.
    virtual void RegisterStandaloneView(IStandaloneView* pView)   = 0;
    virtual void UnregisterStandaloneView(IStandaloneView* pView) = 0;
};

#    define STANDALONEVIEW_MANAGER_INTERFACE_VERSION "StandaloneViewManager_API_001"

#endif //STANDALONE_VIEW_INTERFACE_H
