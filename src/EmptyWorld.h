#pragma once

struct cl_entity_s;
struct model_s;

namespace standaloneview
{
// Until a map is fully loaded, V_RenderView still needs a brush worldmodel: R_RenderView
// rejects a NULL cl.worldmodel or r_worldentity.model, and R_SetupFrame / R_MarkLeaves walk
// its BSP. While connecting, CL_RegisterResources sets cl.worldmodel and redraws the loading
// screen before R_NewMap sets r_worldentity.model and the renderer builds its world data.
// The empty world is a single CONTENTS_EMPTY leaf that is both the root node and leafs[0],
// with no visible leafs.
class EmptyWorldScope
{
public:
    // Installs the empty world into cl.worldmodel and r_worldentity.model unless the world is
    // ready (the client was active) and both pointers are set; the destructor restores both.
    EmptyWorldScope(model_s** worldModel, cl_entity_s* worldEntity, bool worldReady);
    ~EmptyWorldScope();

    EmptyWorldScope(const EmptyWorldScope&)            = delete;
    EmptyWorldScope& operator=(const EmptyWorldScope&) = delete;

    bool IsInstalled() const { return m_bInstalled; }

private:
    model_s**    m_pWorldModel       = nullptr;
    cl_entity_s* m_pWorldEntity      = nullptr;
    model_s*     m_pSavedWorldModel  = nullptr;
    model_s*     m_pSavedEntityModel = nullptr;
    bool         m_bInstalled        = false;
};

model_s* GetEmptyWorldModel();
} // namespace standaloneview
