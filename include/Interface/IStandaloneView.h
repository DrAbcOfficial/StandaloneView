#pragma once

#ifndef STANDALONE_VIEW_INTERFACE_H
#    define STANDALONE_VIEW_INTERFACE_H

#    include <interface.h>

struct cl_entity_s;
struct ref_params_s;

namespace standaloneview
{
// Per-draw studio lighting for IStandaloneViewDrawfuncs::DrawStudioModel, applied at the
// engine's StudioSetupLighting seam while the draw runs: after the engine's dynamic and
// entity lighting, before the values are consumed. The direction is in world
// coordinates, opposite the direction from the model towards the light, and is
// normalized before use.
struct StudioLighting
{
    float color[3]     = {1.0f, 1.0f, 1.0f};
    float direction[3] = {-1.0f, 0.0f, 0.0f};
    int   ambientlight = 96;
    int   shadelight   = 96;
    bool  enabled      = true; // false draws with full ambient and no shadelight
};
} // namespace standaloneview

/*
    Draws entities through the renderer that is installed: the engine's own model
    renderers, or Renderer's replacements. Passed to IStandaloneView::DrawNormalTriangles /
    DrawTransparentTriangles and valid only until that callback returns; using it
    elsewhere is a fatal error.

    The entity's model must match the function. Each call sets currententity (and for
    sprites r_entorigin) and r_blend the way the engine's entity lists do, then restores
    them: r_blend is 1 for kRenderNormal, CL_FxBlend / 255 otherwise, and nothing is
    drawn when that is 0.

    The studio functions take an optional private MOVETYPE_FOLLOW bone source: such
    entities are not registered in the engine's entity array, so aiment cannot reach
    their animation source. A non-null source must have a studio model; its bones are
    evaluated with flags 0 immediately before the entity, because StudioMergeBones
    consumes the last StudioSaveBones result and each follower replaces that cache. An
    invalid or unevaluated source poses and draws nothing. A null source uses the
    entity's own pose.
*/
class IStandaloneViewDrawfuncs
{
public:
    // Dispatches to the engine's R_DrawBrushModel.
    virtual void DrawBrushModel(struct cl_entity_s* ent) = 0;

    // Dispatches to the studio model interface the engine uses (the client's, if it
    // provides one): StudioDrawPlayer for players, StudioDrawModel otherwise, with
    // STUDIO_RENDER and no STUDIO_EVENTS, so a view fires no model events. boneSource may
    // be null (see above). Pass a StudioLighting to draw with that light instead of the
    // world lighting, or null to keep it.
    virtual void DrawStudioModel(struct cl_entity_s* ent, struct cl_entity_s* boneSource, const standaloneview::StudioLighting* lighting) = 0;

    // Dispatches to the engine's R_DrawSpriteModel. A sprite with curstate.body follows
    // attachment body of entity curstate.skin, as in the engine.
    virtual void DrawSpriteModel(struct cl_entity_s* ent) = 0;

    // Evaluates the studio pose DrawStudioModel would draw, without drawing: no culling,
    // no meshes, no events. The engine studio bone transforms remain readable until the
    // next studio call, so a preview can read the posed vertices through
    // StudioGetBoneTransform afterwards. Returns false when the pose was not evaluated.
    virtual bool SetupStudioPose(struct cl_entity_s* ent, struct cl_entity_s* boneSource) = 0;
};

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
    // pDrawfuncs is valid only until the callback returns.
    virtual void DrawNormalTriangles(IStandaloneViewDrawfuncs* pDrawfuncs)      = 0;
    virtual void DrawTransparentTriangles(IStandaloneViewDrawfuncs* pDrawfuncs) = 0;
};

class IStandaloneViewManager : public IBaseInterface
{
public:
    // Registering twice is ignored. A view registered from a callback renders from the next frame.
    virtual void RegisterStandaloneView(IStandaloneView* pView) = 0;
    // Must not be called from any IStandaloneView callback: that is a fatal error.
    // Unregister before destroying the view, outside StandaloneView callbacks.
    virtual void UnregisterStandaloneView(IStandaloneView* pView) = 0;
};

#    define STANDALONEVIEW_MANAGER_INTERFACE_VERSION "StandaloneViewManager_API_003"

#endif //STANDALONE_VIEW_INTERFACE_H
