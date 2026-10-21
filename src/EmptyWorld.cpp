#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <const.h>
#include <cstring>

#include "EmptyWorld.h"

namespace standaloneview
{
namespace
{
constexpr char EmptyWorldName[] = "*standaloneview_empty";

mleaf_t g_EmptyLeaf              = {};
model_t g_EmptyWorld             = {};
bool    g_bEmptyWorldInitialized = false;

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
}

EmptyWorldScope::~EmptyWorldScope()
{
    if (!m_bInstalled)
        return;

    *m_pWorldModel        = m_pSavedWorldModel;
    m_pWorldEntity->model = m_pSavedEntityModel;
}
} // namespace standaloneview
