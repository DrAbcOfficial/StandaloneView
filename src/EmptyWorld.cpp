#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <const.h>
#include <cstring>

#include "EmptyWorld.h"

extern cl_enginefunc_t gEngfuncs;

namespace standaloneview
{
namespace
{
constexpr char EmptyWorldName[] = "*standaloneview_empty";

mleaf_t g_EmptyLeaf              = {};
model_t g_EmptyWorld             = {};
bool    g_bEmptyWorldInitialized = false;

// The empty world carries no BSP visibility data: the engine's leaf and PVS calls take
// their novis path (Mod_LeafPVS returns novis for leafs[0]), but the client's
// StudioDrawModel runs its own leaf test and culls entities while a real visibility
// path is active. Ride the engine's r_novis for as long as the empty world is
// installed; the value is restored before the main view renders.
cvar_t* g_pNoVisCvar   = nullptr;
float   g_flSavedNoVis = 0.0f;

void ArmNoVis()
{
    g_pNoVisCvar   = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer("r_novis") : nullptr;
    if (!g_pNoVisCvar)
        return;
    g_flSavedNoVis      = g_pNoVisCvar->value;
    g_pNoVisCvar->value = 1.0f;
}

void RestoreNoVis()
{
    if (!g_pNoVisCvar)
        return;
    g_pNoVisCvar->value = g_flSavedNoVis;
    g_pNoVisCvar        = nullptr;
}

void InitializeEmptyWorld()
{
    if (g_bEmptyWorldInitialized)
        return;

    g_EmptyLeaf.contents = CONTENTS_EMPTY;

    strncpy(g_EmptyWorld.name, EmptyWorldName, sizeof(g_EmptyWorld.name) - 1);
    g_EmptyWorld.type = mod_brush;
    // Mod_PointInLeaf stops at the root because it is a leaf; Mod_LeafPVS returns
    // mod_novis for leafs[0]; numleafs = 0 leaves nothing for R_MarkLeaves to mark.
    g_EmptyWorld.nodes = reinterpret_cast<mnode_t*>(&g_EmptyLeaf);
    g_EmptyWorld.leafs = &g_EmptyLeaf;

    g_bEmptyWorldInitialized = true;
}
} // namespace

model_t* GetEmptyWorldModel()
{
    InitializeEmptyWorld();
    return &g_EmptyWorld;
}

EmptyWorldScope::EmptyWorldScope(model_t** worldModel, cl_entity_t* worldEntity, bool worldReady) :
    m_pWorldModel(worldModel),
    m_pWorldEntity(worldEntity)
{
    if (!m_pWorldModel || !m_pWorldEntity)
        return;
    if (worldReady && *m_pWorldModel && m_pWorldEntity->model)
        return;

    m_pSavedWorldModel    = *m_pWorldModel;
    m_pSavedEntityModel   = m_pWorldEntity->model;
    *m_pWorldModel        = GetEmptyWorldModel();
    m_pWorldEntity->model = *m_pWorldModel;
    m_bInstalled          = true;
    ArmNoVis();
}

EmptyWorldScope::~EmptyWorldScope()
{
    if (!m_bInstalled)
        return;

    RestoreNoVis();
    *m_pWorldModel        = m_pSavedWorldModel;
    m_pWorldEntity->model = m_pSavedEntityModel;
}
} // namespace standaloneview
