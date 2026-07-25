# `src/utils/uiconstants/`

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

## Inline layout literals in dialogs — promote on touch

There are ~113 bare `setSpacing(8)` / `setContentsMargins(12, …)`-style
literals across `src/ui` and `src/chrome`, against ~21 call sites already
reading a `UIConstants` value. **This is not a backlog to burn down in one
pass.** Converting a dialog you are not otherwise editing produces a wide,
untestable diff over pixel values, and the layout literals that matter are
the ones someone is already looking at.

The rule is opportunistic: **when you touch a dialog for another reason,
promote that dialog's bare layout literals as part of the same change.**

Promote them into **that dialog's own namespace** in `dialog.h`, following
what `ScrapeResultDialog` already does:

```cpp
namespace UIConstants {
namespace MyDialog {
inline constexpr int ROOT_LAYOUT_SPACING = 8;
inline constexpr int SECTION_LAYOUT_SPACING = 6;
} // namespace MyDialog
} // namespace UIConstants
```

Do **not** reach for a shared `UIConstants::Dialog::*` spacing set. It does
not exist — `Dialog` holds only the About dialog's size — and inventing one
would work against the per-domain ownership this directory is built on. Two
dialogs both spacing at 8px are not thereby coupled; a shared constant would
claim they are, and the next person tuning one would silently move the
other. Same-valued is not same-meaning.

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
