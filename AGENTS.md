# AGENTS.md

This file provides guidance and important rules working with code in this repository.

## Project knowledge

Architecture, design decisions, engine facts and verification status live in
`memory/StandaloneView.md` (`StandaloneView`, `permalink: standaloneview/standalone-view`).
Read it before changing the render flow, the public interface or the gamedata manifest.

- Public interface: `include/Interface/IStandaloneView.h`. Keep `StandaloneViewManager_API_001`
  binary compatible; add a new interface version instead of changing an existing vtable.
- Engine symbols are resolved only through gamedata (`ResolveGameSymbol`); update
  `scripts/manifests/standaloneview.json` in the same change. Do not add scan fallbacks.
- Core logic stays in `src/StandaloneViewManager.cpp` behind the injected engine/client
  bridges so `tests/` can exercise it without the game. Regression tests do not replace
  in-game verification; report the two separately.

## C/C++ formatting

Owned C/C++ sources use the shared DiligentCore style from
[MetaHookSv/FormatValidation](https://github.com/MetaHookSv/FormatValidation), pinned
to clang-format 23.1.3. Setup, commands and the checked scope are recorded in
`memory/CppFormatting.md` (`CppFormatting`); the aggregator keeps the same note as
`metahooksv/cpp-formatting`. Do not hand-edit the generated `.clang-format`; change
the shared style. Keep MSVC inline assembly in explicit blocks.
