# `src/ui/uiconstants/`

Header-only `inline constexpr` constants for UI tuning, split per domain
(one header per concern: `animation.h`, `artwork.h`, `grid.h`, …).

## Why per-domain split

A single ownership boundary per concern makes a small set of `constexpr`
values easy to find, easy to change, and cheap to recompile. When one
domain's tuning changes, only TUs that included that header rebuild —
not the world. It also keeps `git blame` honest when a constant moves.

## Convention

```cpp
#ifndef UICONSTANTS_<DOMAIN>_H
#define UICONSTANTS_<DOMAIN>_H

namespace UIConstants {
namespace <Domain> {
inline constexpr int FOO_PX = 12;
} // namespace <Domain>
} // namespace UIConstants

#endif
```

- Header-only — no `.cpp`.
- `inline constexpr` (not `static const`, not `#define`).
- Nested in `UIConstants::<Domain>` — call sites read
  `UIConstants::Artwork::BATCH_HIGH`.
- Constant names are `SCREAMING_SNAKE_CASE`.

## When to add a new header

New `<domain>.h` when the constant belongs to a UI concern not covered by
any existing file. Otherwise add to whichever existing header already owns
the concept (`grid.h` for layout, `animation.h` for durations,
`color.h` for color literals, …).

## When NOT to use this directory

- **Per-widget constants tightly coupled to one widget** — keep them
  with the widget (`static constexpr` in the class, or anon-namespace in
  the `.cpp`).
- **Runtime-mutable values** (anything the user can change) — belong in
  `SettingsManager`.
- **Cross-layer constants** (DB column counts, scraper rate limits, …)
  — belong with their owning module under `src/modules/`.

## Do not consolidate

Resist merging several files into one big `uiconstants.h`. The per-domain
split is the point — it documents ownership and minimizes rebuild scope.
