#ifndef SETUPUTILS_H
#define SETUPUTILS_H

#include "applicationcontext.h"

/**
 * @brief Utilities for setup struct getter boilerplate elimination.
 *
 * Setup structs follow a pattern where each field has a getter that returns
 * the direct field value if set, otherwise falls back to ApplicationContext.
 * This header provides macros to reduce the ~4 lines per getter to 1 line.
 *
 * Usage in header - declare getters:
 *   struct MyManagerSetup {
 *     const ApplicationContext *ctx = nullptr;
 *     ScrollManager *scrollManager = nullptr;
 *     QWidget *gridContainer = nullptr;
 *
 *     SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
 *     SETUP_GETTER_DECL(QWidget*, GridContainer)
 *     SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
 *   };
 *
 * Usage in cpp - define getters:
 *   SETUP_GETTER_DEF(MyManagerSetup, ScrollManager*, ScrollManager, scrollManager, scrollManager)
 *   SETUP_GETTER_DEF(MyManagerSetup, QWidget*, GridContainer, gridContainer, gridContainer)
 *   SETUP_GETTER_DEF_CTX_ONLY(MyManagerSetup, InteractionStateHolder*, InteractionState, interactionState)
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
#define SETUP_GETTER_DECL(TYPE, NAME) \
  [[nodiscard]] auto get##NAME() const -> TYPE;

/**
 * @brief Declares a getter for ctx-only fields (no local field).
 */
#define SETUP_GETTER_DECL_CTX_ONLY(TYPE, NAME) \
  [[nodiscard]] auto get##NAME() const -> TYPE;

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
#define SETUP_GETTER_DEF(STRUCT, TYPE, NAME, FIELD, CTX_FIELD) \
  auto STRUCT::get##NAME() const -> TYPE { \
    return FIELD ? FIELD : (ctx ? ctx->CTX_FIELD : nullptr); \
  }

/**
 * @brief Defines a getter where local field and ctx field have the same name.
 *
 * @param STRUCT The setup struct name
 * @param TYPE   The return type
 * @param NAME   The getter suffix
 * @param FIELD  The field name (same in struct and ctx)
 */
#define SETUP_GETTER_DEF_SAME(STRUCT, TYPE, NAME, FIELD) \
  SETUP_GETTER_DEF(STRUCT, TYPE, NAME, FIELD, FIELD)

/**
 * @brief Defines a getter for ctx-only fields (no local override).
 *
 * @param STRUCT    The setup struct name
 * @param TYPE      The return type
 * @param NAME      The getter suffix
 * @param CTX_FIELD The ApplicationContext field name
 */
#define SETUP_GETTER_DEF_CTX_ONLY(STRUCT, TYPE, NAME, CTX_FIELD) \
  auto STRUCT::get##NAME() const -> TYPE { \
    return ctx ? ctx->CTX_FIELD : nullptr; \
  }

// ─────────────────────────────────────────────────────────────────────────────
// Inline definition macros (for header-only structs or simple cases)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Inline getter definition with local field and ctx fallback.
 *
 * Use when you want the getter defined inline in the header.
 */
#define SETUP_GETTER_INLINE(TYPE, NAME, FIELD, CTX_FIELD) \
  [[nodiscard]] auto get##NAME() const -> TYPE { \
    return FIELD ? FIELD : (ctx ? ctx->CTX_FIELD : nullptr); \
  }

/**
 * @brief Inline getter for same-named field in struct and ctx.
 */
#define SETUP_GETTER_INLINE_SAME(TYPE, NAME, FIELD) \
  SETUP_GETTER_INLINE(TYPE, NAME, FIELD, FIELD)

/**
 * @brief Inline getter for ctx-only fields.
 */
#define SETUP_GETTER_INLINE_CTX_ONLY(TYPE, NAME, CTX_FIELD) \
  [[nodiscard]] auto get##NAME() const -> TYPE { \
    return ctx ? ctx->CTX_FIELD : nullptr; \
  }

#endif // SETUPUTILS_H
