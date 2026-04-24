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
 * the direct field value if set, otherwise falls back to ApplicationContext.
 * This header provides macros to reduce the ~4 lines per getter to 1 line.
 *
 * ## Macro Categories
 *
 * ### Declaration Macros (for .h files)
 * - `SETUP_GETTER_DECL(TYPE, NAME)` - Declares getter with local + ctx fallback
 * - `SETUP_GETTER_DECL_CTX_ONLY(TYPE, NAME)` - Declares getter for ctx-only
 * field
 *
 * ### Definition Macros (for .cpp files)
 * - `SETUP_GETTER_DEF(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)` - Full definition
 * - `SETUP_GETTER_DEF_SAME(STRUCT, TYPE, NAME, FIELD)` - When field names match
 * - `SETUP_GETTER_DEF_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)` - Ctx-only field
 *
 * ### Inline Macros (for header-only definitions)
 * - `SETUP_GETTER_INLINE(TYPE, NAME, FIELD, CTX_FIELD)` - Inline with fallback
 * - `SETUP_GETTER_INLINE_SAME(TYPE, NAME, FIELD)` - Inline, same field name
 * - `SETUP_GETTER_INLINE_CTX_ONLY(TYPE, NAME, CTX_FIELD)` - Inline ctx-only
 *
 * ## Example Usage
 *
 * In header file (e.g., mymanager.h):
 * @code
 *   struct MyManagerSetup {
 *     const ApplicationContext *ctx = nullptr;
 *     ScrollManager *scrollManager = nullptr;   // Can be overridden locally
 *     QWidget *gridContainer = nullptr;         // Can be overridden locally
 *     // InteractionStateHolder comes only from ctx, no local field
 *
 *     SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
 *     SETUP_GETTER_DECL(QWidget*, GridContainer)
 *     SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
 *   };
 * @endcode
 *
 * In source file (e.g., mymanager.cpp):
 * @code
 *   // Field names match between struct and ctx:
 *   SETUP_GETTER_DEF_SAME(MyManagerSetup, ScrollManager*, ScrollManager,
 * scrollManager) SETUP_GETTER_DEF_SAME(MyManagerSetup, QWidget*, GridContainer,
 * gridContainer)
 *
 *   // Field only exists in ctx:
 *   SETUP_GETTER_DEF_CTX_ONLY(MyManagerSetup, InteractionStateHolder*,
 * InteractionState, interactionState)
 *
 *   // Different field names in struct vs ctx:
 *   SETUP_GETTER_DEF(MyManagerSetup, QScrollArea*, MediaScrollArea,
 * mediaScrollArea, itemScrollArea)
 * @endcode
 *
 * ## Fallback Behavior
 *
 * 1. If the local struct field is non-null, it's returned (allows override)
 * 2. Otherwise, if ctx is non-null, the corresponding ctx field is returned
 * 3. Otherwise, nullptr is returned
 *
 * This allows manager setup to either use shared context values or override
 * specific fields when needed (e.g., for owned sub-managers).
 */

// ─────────────────────────────────────────────────────────────────────────────
// Header-only declaration macros (for use in .h files)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Declares a getter method in a setup struct header.
 *
 * @param TYPE   The return type (e.g., ScrollManager*)
 * @param NAME   The getter suffix (e.g., ScrollManager -> getScrollManager())
 */
#define SETUP_GETTER_DECL(TYPE, NAME) [[nodiscard]] auto get##NAME() const -> TYPE;

/**
 * @brief Declares a getter for ctx-only fields (no local field).
 */
#define SETUP_GETTER_DECL_CTX_ONLY(TYPE, NAME) [[nodiscard]] auto get##NAME() const -> TYPE;

// ─────────────────────────────────────────────────────────────────────────────
// Implementation definition macros (for use in .cpp files)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Defines a getter with local field and ctx fallback.
 *
 * @param STRUCT    The setup struct name (e.g., KeyboardManagerSetup)
 * @param TYPE      The return type
 * @param NAME      The getter suffix (e.g., ScrollManager)
 * @param FIELD     The local field name (e.g., scrollManager)
 * @param CTX_FIELD The ApplicationContext field name (e.g., scrollManager)
 */
#define SETUP_GETTER_DEF(STRUCT, TYPE, NAME, FIELD, CTX_FIELD)                                     \
  auto STRUCT::get##NAME() const->TYPE {                                                           \
    return FIELD ? FIELD : (ctx ? ctx->CTX_FIELD : nullptr);                                       \
  }

/**
 * @brief Defines a getter where local field and ctx field have the same name.
 *
 * @param STRUCT The setup struct name
 * @param TYPE   The return type
 * @param NAME   The getter suffix
 * @param FIELD  The field name (same in struct and ctx)
 */
#define SETUP_GETTER_DEF_SAME(STRUCT, TYPE, NAME, FIELD)                                           \
  SETUP_GETTER_DEF(STRUCT, TYPE, NAME, FIELD, FIELD)

/**
 * @brief Defines a getter for ctx-only fields (no local override).
 *
 * @param STRUCT    The setup struct name
 * @param TYPE      The return type
 * @param NAME      The getter suffix
 * @param CTX_FIELD The ApplicationContext field name
 */
#define SETUP_GETTER_DEF_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD)                                   \
  auto STRUCT::get##NAME() const->TYPE {                                                           \
    return ctx ? ctx->CTX_FIELD : nullptr;                                                         \
  }

// ─────────────────────────────────────────────────────────────────────────────
// Inline definition macros (for header-only structs or simple cases)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Inline getter definition with local field and ctx fallback.
 *
 * Use when you want the getter defined inline in the header.
 */
#define SETUP_GETTER_INLINE(TYPE, NAME, FIELD, CTX_FIELD)                                          \
  [[nodiscard]] auto get##NAME() const -> TYPE {                                                   \
    return FIELD ? FIELD : (ctx ? ctx->CTX_FIELD : nullptr);                                       \
  }

/**
 * @brief Inline getter for same-named field in struct and ctx.
 */
#define SETUP_GETTER_INLINE_SAME(TYPE, NAME, FIELD) SETUP_GETTER_INLINE(TYPE, NAME, FIELD, FIELD)

/**
 * @brief Inline getter for ctx-only fields.
 */
#define SETUP_GETTER_INLINE_CTX_ONLY(TYPE, NAME, CTX_FIELD)                                        \
  [[nodiscard]] auto get##NAME() const -> TYPE {                                                   \
    return ctx ? ctx->CTX_FIELD : nullptr;                                                         \
  }

#endif // SETUPUTILS_H
