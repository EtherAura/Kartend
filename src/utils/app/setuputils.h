#ifndef SETUPUTILS_H
#define SETUPUTILS_H

#include "applicationcontext.h"

/**
 * @file setuputils.h
 * @brief Macros for reducing setup struct getter boilerplate.
 *
 * ## Overview
 *
 * Setup structs are used for dependency injection into manager classes.
 * Each manager has a corresponding setup struct (e.g., `ScrollManagerSetup`)
 * that aggregates the dependencies needed by that manager.
 *
 * Setup structs follow a pattern where each field has a getter that returns
 * the direct field value if set, otherwise falls back to one of the three
 * `ApplicationContext` sub-structs (`collection`, `ui`, `managers`).
 *
 * Pick the macro variant matching the sub-struct that owns the fallback
 * field:
 *
 *   - `*_COL_*`  → falls back to `ctx->collection.<field>`
 *   - `*_UI_*`   → falls back to `ctx->ui.<field>`
 *   - `*_MGR_*`  → falls back to `ctx->managers.<field>`
 *
 * ## Macro Categories
 *
 * ### Declaration (for .h files)
 *   `SETUP_GETTER_DECL(TYPE, NAME)` — Declares a getter
 *   `SETUP_GETTER_DECL_CTX_ONLY(TYPE, NAME)` — Same, no local override
 *
 * ### Definition (for .cpp files), section-aware
 *   `SETUP_GETTER_DEF_<SECTION>(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)`
 *   `SETUP_GETTER_DEF_<SECTION>_SAME(STRUCT, TYPE, NAME, FIELD)`
 *   `SETUP_GETTER_DEF_<SECTION>_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)`
 *
 *   where `<SECTION>` is `COL`, `UI`, or `MGR`.
 *
 * ### Inline (for header-only definitions), section-aware
 *   `SETUP_GETTER_INLINE_<SECTION>(TYPE, NAME, FIELD, CTX_FIELD)`
 *   `SETUP_GETTER_INLINE_<SECTION>_SAME(TYPE, NAME, FIELD)`
 *   `SETUP_GETTER_INLINE_<SECTION>_CTX_ONLY(TYPE, NAME, CTX_FIELD)`
 *
 * ## Example Usage
 *
 * Header (mymanager.h):
 * @code
 *   struct MyManagerSetup {
 *     const ApplicationContext *ctx = nullptr;
 *     ScrollManager *scrollManager = nullptr;
 *     QWidget *gridContainer = nullptr;
 *
 *     SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
 *     SETUP_GETTER_DECL(QWidget*, GridContainer)
 *     SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
 *   };
 * @endcode
 *
 * Source (mymanager.cpp):
 * @code
 *   SETUP_GETTER_DEF_MGR_SAME(MyManagerSetup, ScrollManager*,
 *                             ScrollManager, scrollManager)
 *   SETUP_GETTER_DEF_UI_SAME(MyManagerSetup, QWidget*,
 *                            GridContainer, gridContainer)
 *   SETUP_GETTER_DEF_MGR_CTX_ONLY(MyManagerSetup, InteractionStateHolder*,
 *                                 InteractionState, interactionState)
 * @endcode
 *
 * ## Fallback Behavior
 *
 * 1. If the local struct field is non-null, it's returned (allows override)
 * 2. Otherwise, if ctx is non-null, the corresponding ctx sub-struct field
 *    is returned
 * 3. Otherwise, nullptr is returned
 */

// clang-format off
// Trailing-return-type arrows inside macros confuse clang-format's pointer
// alignment heuristics (Right) — the formatter eats the spaces around `->`
// and produces unreadable `const->TYPE`. Keep formatting hands-off here.
// ─────────────────────────────────────────────────────────────────────────────
// Header-only declaration macros (for use in .h files)
// ─────────────────────────────────────────────────────────────────────────────

#define SETUP_GETTER_DECL(TYPE, NAME) [[nodiscard]] auto get##NAME() const -> TYPE;

#define SETUP_GETTER_DECL_CTX_ONLY(TYPE, NAME) [[nodiscard]] auto get##NAME() const -> TYPE;

// ─────────────────────────────────────────────────────────────────────────────
// Implementation definition macros (for use in .cpp files)
// ─────────────────────────────────────────────────────────────────────────────

// Generic section-aware definition (used internally by section variants)
#define SETUP_GETTER_DEF_SECTION(STRUCT, TYPE, NAME, FIELD, SECTION, CTX_FIELD)                    \
  auto STRUCT::get##NAME() const -> TYPE {                                                         \
    return FIELD ? FIELD : (ctx ? ctx->SECTION.CTX_FIELD : nullptr);                               \
  }

#define SETUP_GETTER_DEF_SECTION_CTX_ONLY(STRUCT, TYPE, NAME, SECTION, CTX_FIELD)                  \
  auto STRUCT::get##NAME() const -> TYPE {                                                         \
    return ctx ? ctx->SECTION.CTX_FIELD : nullptr;                                                 \
  }

// Collection section
#define SETUP_GETTER_DEF_COL(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)                                 \
  SETUP_GETTER_DEF_SECTION(STRUCT, TYPE, NAME, FIELD, collection, CTX_FIELD)
#define SETUP_GETTER_DEF_COL_SAME(STRUCT, TYPE, NAME, FIELD)                                       \
  SETUP_GETTER_DEF_COL(STRUCT, TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_DEF_COL_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)                               \
  SETUP_GETTER_DEF_SECTION_CTX_ONLY(STRUCT, TYPE, NAME, collection, CTX_FIELD)

// UI section
#define SETUP_GETTER_DEF_UI(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)                                  \
  SETUP_GETTER_DEF_SECTION(STRUCT, TYPE, NAME, FIELD, ui, CTX_FIELD)
#define SETUP_GETTER_DEF_UI_SAME(STRUCT, TYPE, NAME, FIELD)                                        \
  SETUP_GETTER_DEF_UI(STRUCT, TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_DEF_UI_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)                                \
  SETUP_GETTER_DEF_SECTION_CTX_ONLY(STRUCT, TYPE, NAME, ui, CTX_FIELD)

// Manager section
#define SETUP_GETTER_DEF_MGR(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)                                 \
  SETUP_GETTER_DEF_SECTION(STRUCT, TYPE, NAME, FIELD, managers, CTX_FIELD)
#define SETUP_GETTER_DEF_MGR_SAME(STRUCT, TYPE, NAME, FIELD)                                       \
  SETUP_GETTER_DEF_MGR(STRUCT, TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_DEF_MGR_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)                               \
  SETUP_GETTER_DEF_SECTION_CTX_ONLY(STRUCT, TYPE, NAME, managers, CTX_FIELD)

// ─────────────────────────────────────────────────────────────────────────────
// Inline definition macros (for header-only structs or simple cases)
// ─────────────────────────────────────────────────────────────────────────────

#define SETUP_GETTER_INLINE_SECTION(TYPE, NAME, FIELD, SECTION, CTX_FIELD)                         \
  [[nodiscard]] auto get##NAME() const -> TYPE {                                                   \
    return FIELD ? FIELD : (ctx ? ctx->SECTION.CTX_FIELD : nullptr);                               \
  }

#define SETUP_GETTER_INLINE_SECTION_CTX_ONLY(TYPE, NAME, SECTION, CTX_FIELD)                       \
  [[nodiscard]] auto get##NAME() const -> TYPE {                                                   \
    return ctx ? ctx->SECTION.CTX_FIELD : nullptr;                                                 \
  }

// Collection section
#define SETUP_GETTER_INLINE_COL(TYPE, NAME, FIELD, CTX_FIELD)                                      \
  SETUP_GETTER_INLINE_SECTION(TYPE, NAME, FIELD, collection, CTX_FIELD)
#define SETUP_GETTER_INLINE_COL_SAME(TYPE, NAME, FIELD)                                            \
  SETUP_GETTER_INLINE_COL(TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_INLINE_COL_CTX_ONLY(TYPE, NAME, CTX_FIELD)                                    \
  SETUP_GETTER_INLINE_SECTION_CTX_ONLY(TYPE, NAME, collection, CTX_FIELD)

// UI section
#define SETUP_GETTER_INLINE_UI(TYPE, NAME, FIELD, CTX_FIELD)                                       \
  SETUP_GETTER_INLINE_SECTION(TYPE, NAME, FIELD, ui, CTX_FIELD)
#define SETUP_GETTER_INLINE_UI_SAME(TYPE, NAME, FIELD)                                             \
  SETUP_GETTER_INLINE_UI(TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_INLINE_UI_CTX_ONLY(TYPE, NAME, CTX_FIELD)                                     \
  SETUP_GETTER_INLINE_SECTION_CTX_ONLY(TYPE, NAME, ui, CTX_FIELD)

// Manager section
#define SETUP_GETTER_INLINE_MGR(TYPE, NAME, FIELD, CTX_FIELD)                                      \
  SETUP_GETTER_INLINE_SECTION(TYPE, NAME, FIELD, managers, CTX_FIELD)
#define SETUP_GETTER_INLINE_MGR_SAME(TYPE, NAME, FIELD)                                            \
  SETUP_GETTER_INLINE_MGR(TYPE, NAME, FIELD, FIELD)
#define SETUP_GETTER_INLINE_MGR_CTX_ONLY(TYPE, NAME, CTX_FIELD)                                    \
  SETUP_GETTER_INLINE_SECTION_CTX_ONLY(TYPE, NAME, managers, CTX_FIELD)
// clang-format on

#endif // SETUPUTILS_H
