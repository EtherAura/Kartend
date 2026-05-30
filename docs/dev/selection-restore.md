# Selection-restore state machine

`SelectionRestoreCoordinator` ([src/modules/data/restore/](../../src/modules/data/restore/))
restores the user's last selection when navigating between
collections. Most of the complexity is **defending against stale
restores** racing with subsequent user input — a slow restore on
collection A shouldn't override a click the user already made on
collection B.

## When restoration happens

Three triggers:

| Trigger | Source | Restore source |
|---------|--------|----------------|
| App launch with `rememberSelection=true` | `MainWindow` after the first collection load completes | `GeneralSettings::lastSelectedItems[collectionIndex]` |
| Navigating into a subcollection | `NavigationManager` push | Same map |
| Back / up navigation | `NavigationManager` pop | The selection saved when the user navigated into the child |

Each trigger calls
`SelectionRestoreCoordinator::scheduleSelectionRestore(desiredIndex,
finalEnsureDelayMs)`.

## The token-based race defense

Stale restores are the dominant failure mode — items load
asynchronously through the worker thread, so a restore scheduled
when navigating into collection A can fire *after* the user has
already navigated to B. Without defense, the late restore would
overwrite B's selection with A's last-selected index (which doesn't
even refer to a B item).

The defense is a per-collection-navigation **token**:

```
scheduleSelectionRestore
        │
        ▼
initializeSelectionRestoreToken() ──▶ new token; stored on InteractionStateHolder
        │
        ▼
queue a deferred restore lambda with [scheduledCollectionIndex, token]
        │
        ▼
        ◇ deferred lambda fires after finalEnsureDelayMs
        │
        ▼
createRestoreValidationLambda(scheduledCollectionIndex, token):
   - state still holds the same token? (no newer schedule ran)
   - currentCollectionIndex == scheduledCollectionIndex?
   - not shutting down?
        │
        ▼
   yes → executeSelectionRestore(desiredIndex, ...)
    no → silently drop (the restore was superseded)
```

`initializeSelectionRestoreToken()` writes the new token to
`InteractionStateHolder::selectionRestoreToken`. Any subsequent
`scheduleSelectionRestore` call rotates the token, so all already-
queued lambdas validate against an old value and silently no-op.

The validation lambda is created at schedule time and captures the
scheduled collection index + token by value. When it fires, the
current collection / token must still match — that's the "this
restore is still valid" predicate.

## Why a final-ensure delay

`scheduleSelectionRestore(desiredIndex, finalEnsureDelayMs)` doesn't
restore immediately. It schedules the restore through `TimerUtils`
with `finalEnsureDelayMs` of slack. The reason is **the items the
restore targets might not be loaded yet** — the QueryManager worker
emits batches; the visible grid populates over hundreds of ms after
a navigation.

If the desired index isn't yet materialized when the timer fires,
the executor:

1. Sets the selection on whatever **is** visible, as close to the
   desired index as possible.
2. Schedules a verification pass at the next finalEnsure interval.
3. The verification lambda checks the desired index again and, if
   the items are now loaded, re-issues the restore.

This is the historical reason the API used to take
`maxAttempts + attemptDelayMs` — multiple attempts as the worker
batched items in. Those parameters were dropped during a later
cleanup because they were always `Q_UNUSED` on receipt (the actual
loop lived elsewhere); the single `finalEnsureDelayMs` covers the
same ground.

## `shouldRestoreSelection`

Restore is **suppressed** in two cases even when scheduled:

1. **`rememberSelection=false`** — the user opted out.
2. **The search bar has focus and is non-empty** — the user is
   actively typing a filter; restoring would yank the visible row
   under their cursor. The search bar's `text` and focus state are
   checked at execute time, not schedule time, so a restore queued
   before the user started searching still defers if they're
   actively filtering when it fires.

## Where the selection is saved

When the user selects an item, the `InteractionManager` writes
through to:

- `GeneralSettings::lastSelectedItems[collectionIndex] = newIndex`
- The settings file is written on dialog Save or at app exit.

So a graceful exit picks up the last-selected; a crash mid-session
loses the post-last-Save selection. That's intentional — fsync'ing
on every selection move would thrash the disk.

## Interactions with other managers

| Interaction | What |
|-------------|------|
| `SelectionManager` | The actual setSelectedIndex call — the coordinator orchestrates *when*, `SelectionManager` orchestrates the visual / overlay change. |
| `NavigationStackManager` | Provides the previous-selection-on-pop. The stack and the restore map serve different purposes — pop restores the *immediately previous* selection, the map restores the *last-ever* selection for that collection. |
| `ScrollManager` | The viewport-centering call to bring the restored item into view fires after the restore lands. |
| `InteractionStateHolder` | Holds the live token. Single source of truth for "what's the most recent restore". |

## Failure modes & how the defense holds

| Race | Defense |
|------|---------|
| User navigates A → B before A's restore fires | Token mismatch — the A-restore is silently dropped. |
| Restore fires after the app is shutting down | `isShuttingDown()` callback returns true — restore is dropped. |
| Restored index doesn't exist (collection shrank since last save) | `getSelectionRestoreIndex` returns -1; coordinator no-ops. |
| User types in the search bar while restore is queued | `shouldRestoreSelection` returns false at execute time. |

## Adding to / extending

Don't add new triggers that **bypass** `scheduleSelectionRestore`.
Any code path that wants to set a selection programmatically and
expects the user's pre-existing selection to take precedence should
route through the coordinator so the race defense stays consistent.

If you need a synchronous restore (no defer), there's no public API
for that today and there shouldn't be — synchronous restore re-
introduces the original race. Add a new schedule helper if you
need a different delay shape; keep the token machinery.

## Related code

| Concern | File |
|---------|------|
| Coordinator | [src/modules/data/restore/selectionrestoremanager.{h,cpp}](../../src/modules/data/restore/) |
| Helpers | [src/modules/data/restore/selectionrestorehelpers.cpp](../../src/modules/data/restore/selectionrestorehelpers.cpp) |
| Token storage | `InteractionStateHolder::selectionRestoreToken` in [src/utils/app/interactionstateholder.h](../../src/utils/app/interactionstateholder.h) |
| Selection write | `SelectionManager` ([src/modules/input/selection/](../../src/modules/input/selection/)) |
| Persistence | `GeneralSettings::lastSelectedItems` in [src/utils/app/collection/generalsettings.h](../../src/utils/app/collection/generalsettings.h) |
| Timer scheduling | [src/utils/threading/timerutils.{h,cpp}](../../src/utils/threading/) |
