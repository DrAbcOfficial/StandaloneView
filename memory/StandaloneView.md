---
title: StandaloneView
type: note
permalink: standaloneview/standalone-view
tags:
- standaloneview
- v_renderview
- gamedata
- architecture
---

# StandaloneView

Plugin from [MetaHookSv issue #942](https://github.com/MetaHookSv/MetaHookSv/issues/942):
other plugins render extra 3D views through the engine's own `V_RenderView`, without
depending on Renderer. It replaces the in-Renderer approach of Renderer PR #18.

## Source entry points

- `include/Interface/IStandaloneView.h`: `IStandaloneView` (consumer callbacks) and
  `IStandaloneViewManager` (`StandaloneViewManager_API_001`). The issue named both
  classes `IStandaloneView`; the manager was renamed.
- `src/StandaloneViewManager.cpp`: registry, render loop, `V_CalcRefdef` and triangle
  routing, behind `standaloneview::EngineBridge` / `ClientBridge` for tests.
- `src/EmptyWorld.cpp`: `EmptyWorldScope`, the empty worldmodel for frames without a map.
- `src/privatehook.cpp`: gamedata resolution, inline hooks (engine, and the Sven client's
  portal manager), client export replacement.
- `src/plugins.cpp`: `IPluginsV4` lifecycle and the interface export.

## Render flow

`VGui_ViewportPaintBackground` hook counts its depth and calls the original, which
calls `V_RenderView`. The `V_RenderView` hook renders views only inside that call,
when not already rendering views, with at least one view registered, and outside
demo playback; then it always calls the original for the main view.

Per frame: `ClientStateScope` forces `cls.state = 5`, `cls.signon = 2`;
`EmptyWorldScope` installs the empty world unless the client was active (checked before
forcing) with both `cl.worldmodel` and `r_worldentity.model` set, and restores both
pointers afterwards; each registered
active view gets `OnBeforeRenderView`, the original `V_RenderView`,
`OnAfterRenderView`. During the pass `V_CalcRefdef` calls only the view's
`CalcRefdef` and forces `onlyClientDraw = 1`, `nextView = 0`; the triangle exports
call only the view's callbacks.

## Decisions

- **Views before the main view.** Each `V_RenderView` zeroes `r_soundOrigin`
  (restored only by passes without `onlyClientDraw`), calls `CL_SetDemoViewInfo`
  when recording, and `V_GetRefParams` writes `cl.viewangles` / `simorg` /
  `punchangle` back. The main view, last, restores them. Renderer's
  `R_RenderView_SvEngine` flushes its final buffer when `viewIdx == 0`, i.e. at
  the start of every `V_RenderView`; rendering views first means that flush
  removes view leftovers instead of the main view.
- **Paint-background gate.** BulletPhysics calls `V_RenderView` from `HUD_Frame`
  to refresh the refdef while its `R_RenderView` hook suppresses drawing; without
  the gate the views would run a wasted pass there.
- **Client `V_CalcRefdef` skipped for views.** Its bob, punch decay, pitch drift and
  step smoothing advance per call.
- **Demo playback skipped.** `CL_GetDemoViewInfo` overwrites the refdef after
  `ClientDLL_CalcRefdef`. `gEngfuncs.pDemoAPI->IsPlayingback()` is the check;
  `cls.demoplayback` is not published.
- **No clearing between views (user decision, 2026-10-21).** The issue's
  `R_Clear` call was dropped: svencoop-10257 and hl-10210 inline it into
  `R_RenderView`, so there is no function to publish. Each `R_RenderView` still
  clears depth (and color with `gl_clear`) at its start; consumers clear their
  viewport themselves.
- **Triangle callbacks (user decision).** With `onlyClientDraws`, neither the engine
  nor Renderer draws the world, `cl_visedicts` or the trans list; only
  `ClientDLL_DrawNormalTriangles` / `DrawTransparentTriangles` run, so the interface
  carries `DrawNormalTriangles` / `DrawTransparentTriangles`.
- **Empty world (user decision).** A static `model_t` (`mod_brush`) whose `nodes`
  and `leafs` point to one `CONTENTS_EMPTY` leaf with `numleafs = 0`:
  `Mod_PointInLeaf` returns at the root, `Mod_LeafPVS` returns novis for
  `leafs[0]`, and `R_MarkLeaves` marks nothing. Restored right after the views.
- **Load order.** `LoadClient` runs in reverse `plugins.lst` order; listing
  StandaloneView first makes its export wrappers outermost.
- **Loading world replaced too.** `CL_RegisterResources` sets `cl.worldmodel`, then
  `ContinueLoadingProgressBar` redraws a frame, and only then `R_NewMap` sets
  `r_worldentity.model` (HLND2T `cl_parse.c`, `gl_rmisc.c`). A pass in that window hit
  Renderer's `R_RenderView: NULL worldmodel` Sys_Error; any frame of a non-active client
  now uses the empty world.
- **Sven client portals skipped during a pass.** SvEngine's `V_RenderView` calls the
  client portal manager (engine global, vtable +0xC) before `ClientDLL_CalcRefdef`.
  The client's entry lazily creates the manager, calls `ClientPortalManager_ResetAll`
  on creation, then `ClientPortalManager_RenderPortals`. StandaloneView hooks both
  (client gamedata, svencoop-10257/8948 only, gated on `SCClientDLL001`) and returns
  early while a view is current. Main-menu crashes without the hooks: `RenderPortals`
  dereferences map state; `ResetAll` calls glew pointers the client initializes only on
  map load (call to address 0 without Renderer; Renderer hooks `ResetAll` itself).
  `HUD_VidInit` calls `ResetAll` unconditionally per map, so skipping it loses nothing.

## Lessons

- **MetaHook defers inline hooks requested in `LoadEngine` / `LoadClient`.** The
  trampoline (`*pOrginalCall`) is written at `MH_TransactionHookCommit`, after all
  plugins returned. Copying it into a bridge at install time stored NULL: the view pass
  and main view never rendered (black screen). Read the trampoline at call time
  (`CallOriginalRenderView`, portal hooks); `hooks_test` defers its fake commit to
  catch this.
- **HUD sprites need the first `HUD_VidInit`.** `SPR_Load` returns 0 until
  `ClientDLL_HudVidInit` runs `SPR_Init`; TriAPI sprite textures are unavailable at the
  main menu. Under Renderer, TriAPI ignores textures bound with `glBindTexture`.

## Engine facts (IDA, 2026-10-21)

- Sven client 10257: portal vtable slot +0xC → `sub_10011020` → `sub_1004AD40`
  (creates the manager in `dword_1063C800` via `sub_1004D9B0`, which calls
  `ResetAll` at 0x1004dcbb, then `RenderPortals` at RVA 0x4e4f0); `HUD_VidInit` →
  `sub_1004ACC0` → `ResetAll`. The engine-side portal pointer is not published.

- svencoop-10257 `V_RenderView` (RVA 0xcdf60): `cmp cls.state, 5`, `cmp cls.signon, 2`;
  its only caller is `VGui_ViewportPaintBackground(int*)`; it calls `R_RenderView(viewnum)`.
- hl-10210 `V_RenderView`: additionally gates on `con_forcedup`, which is only read
  (`Key_Event`, `V_RenderView`) and never written in that build. The HLND2T GL source
  likewise never assigns it.
- Both builds inline `R_Clear` into `R_RenderView`.
- Catalog (11 Windows identities) publishes `V_RenderView`,
  `VGui_ViewportPaintBackground`, `cls.state`, `cls.signon`, `cl_worldmodel`,
  `r_worldentity`; not `R_Clear`, `ClientDLL_CalcRefdef`, `cls.demoplayback`.

## Renderer interaction

- Renderer does not hook `V_RenderView`; its `R_RenderView` hook runs inside each pass.
- `onlyClientDraws` passes skip world, entities, particles, view model, fog,
  post-processing and deferred lighting, and are alpha-composited into
  `s_FinalBufferFBO`, still bound after the pass.
- `R_RenderWaterPass` does not check `onlyClientDraws`: with the empty world
  `R_GetWorldSurfaceModel` returns NULL (the model is not in `mod_known`) and is
  dereferenced; with a real world water references accumulate per view. Renderer
  needs an early return for `onlyClientDraws` there (Renderer branch
  `fix/onlyclientdraws-water-pass`).
- Renderer hooks `ClientPortalManager_RenderPortals` and `ResetAll` itself; the
  StandaloneView hooks chain with them.

## Verification

- 2026-10-21: standalone Debug/Release build with gamedata sync and validation
  (11 snapshots), CTest `standaloneview_manager` / `standaloneview_hooks` passed
  in both configurations, `format-check` clean (10 files). A mutation that renders
  views after the main view made the manager test fail.
- 2026-10-21: portal hooks and the loading-world fix covered by `hooks_test` /
  `manager_test`; mutations (no portal skip, no `ResetAll` skip, old NULL-only world
  check) each made the tests fail.
- 2026-10-21 in game, Sven Co-op 10257 with a scratch consumer (256x256 view, red
  TriAPI quad at the center, green `glScissor` clear at a corner, `glReadPixels` in
  `OnAfterRenderView`), callbacks counted once per frame each:
  - without Renderer: main menu (GL-texture quad) red; `map stadium4` red; `disconnect`
    back to the menu without crash;
  - with Renderer (+ water fix): main menu corner green (pass composited; untextured
    TriAPI draws nothing there); in map center red, corner green, main view intact
    with no view leftovers; map load, `disconnect`, reload without crash.
- Not verified: Half-Life 25th anniversary build, BulletPhysics third person, demo
  playback, Release build in game.
