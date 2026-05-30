# Adding a new INI setting

End-to-end walkthrough for adding a new configuration key, from the
storage struct to the Settings Dialog to the wiki page that documents
it.

User-facing reference for the wire format:
[Configuration Reference](../user/Configuration-Reference.md). High-
level architecture: [architecture.md](architecture.md).

## What changes

A single new key typically touches all of these:

| File | What goes here |
|------|----------------|
| [settingskeys.h](../../src/utils/app/settingskeys.h) | The string constant for the on-disk INI key name |
| One of the leaf headers in [src/utils/app/collection/](../../src/utils/app/collection/) | The C++ struct field + default value |
| The matching `*_persistence.cpp` next to the leaf header, or [collection/helpers.cpp](../../src/utils/app/collection/helpers.cpp) | Validation / clamp helpers (if the field has a valid range) |
| [configvalidation.cpp](../../src/utils/fs/configvalidation.cpp) | Schema validation (if it's per-collection and shouldn't be rejected silently) |
| [settingsmanager.cpp](../../src/modules/data/settings/settingsmanager.cpp) or [settingsmanagercollections.cpp](../../src/modules/data/settings/settingsmanagercollections.cpp) | `QSettings::value(key, default).toType()` on load, `setValue(key, val)` on save |
| The relevant `*panel.ui` in [src/ui/dialogs/settings/](../../src/ui/dialogs/settings/) | The widget |
| The matching `*panel.cpp` | `SettingsFormBinding::loadInto` / `bindCheckbox` / `bindSpinBox` calls |
| [applysettingsdialog.cpp](../../src/ui/dialogs/settings/core/applysettingsdialog.cpp) | The propagation gate (per-collection keys only) |
| [docs/user/Configuration-Reference.md](../user/Configuration-Reference.md) | A row in the right section |

Most additions don't need all of these — pick what applies.

## Decision tree

### Per-collection or global?

| Question | Per-collection | Global (`GeneralSettings`) |
|----------|----------------|----------------------------|
| Should two collections be able to differ? | ✅ | ❌ |
| Does the value belong to the app, not the library? | ❌ | ✅ |
| Examples | `mediaDirectory`, `viewType`, `backgroundColor` | `pixmapCacheSizeMB`, `keyboardRepeatIntervalMs`, `marqueeEnabled` |

### Which leaf cluster (per-collection)?

After the `collectionutils.h` god-header was peeled into leaf
clusters, per-collection settings live in narrow leaf headers — pick
the one your field belongs to:

| Leaf header | Holds |
|-------------|-------|
| `archiveoptions.h` | Archive extraction toggles |
| `collectionbackground.h` | Backgrounds, palette, vignette, parallax, header logo |
| `collectionfilterpreferences.h` | Per-collection title-cleanup regex |
| `folderbrowsingoptions.h` | Virtual-folder toggles + runtime cursor |
| `gridlayoutpreferences.h` | Grid sizing, spacing, item box, scrollbar toggles |
| `launcherconfig.h` | `LauncherConfig` / `LauncherProfile` |
| `listviewoptions.h` | List-view appearance |
| `scraperoverrides.h` | Per-collection scraper / DAT overrides |
| `sidebarappearance.h` | Sidebar visibility, dock, colors, fonts |
| `collectionconfig.h` | Everything that doesn't fit the leaves (the "god-struct" that embeds them) |

Add to a leaf. If your field doesn't fit any leaf cleanly, add it to
`collectionconfig.h` as a flat field and let a future refactor extract
it.

## The walkthrough

This example adds a per-collection `myNewKey` boolean to the
appearance settings.

### 1. Add the INI key constant

[settingskeys.h](../../src/utils/app/settingskeys.h):

```cpp
inline constexpr auto kMyNewKey = "myNewKey";
```

Keys are alphabetically ordered. The string is the **wire format**;
renaming it is a config-migration story. The C++ identifier (the
`k…` constant) is just a refactor — rename freely.

### 2. Add the struct field

Pick the right leaf header (in this example,
`gridlayoutpreferences.h`):

```cpp
struct GridLayoutPreferences {
  // ...existing fields...

  /// Short doc-comment explaining the field.
  /// Mention the default, the valid range (if any), and the reason
  /// it exists.
  bool myNewKey = false;
};
```

Defaults go right next to the declaration. Empty strings for paths;
sensible numeric defaults; `false` for new toggles so an upgrading
install picks up no surprise change in behavior.

### 3. Add validation (optional)

For numeric fields with a sensible range, add a clamp in the
`clampValues()` member on the leaf struct (in the matching
`collection/<leaf>.h` or its `*_persistence.cpp`):

```cpp
config.gridLayoutPreferences.myNumeric =
    std::clamp(config.gridLayoutPreferences.myNumeric, 1, 100);
```

For paths or strings, add validation in
[configvalidation.cpp](../../src/utils/fs/configvalidation.cpp).

### 4. Wire load + save

[settingsmanagercollections.cpp](../../src/modules/data/settings/settingsmanagercollections.cpp)
(or `settingsmanager.cpp` for global keys).

**Load:**

```cpp
config.gridLayoutPreferences.myNewKey =
    settings.value(keys::kMyNewKey, false).toBool();
```

**Save:**

```cpp
settings.setValue(keys::kMyNewKey, c.gridLayoutPreferences.myNewKey);
```

Use the **default value here** matches the struct default. They can
diverge for legitimate reasons (the struct default is what you get
on a fresh build; the load default is what you get on an upgrade),
but defaulting both to the same value avoids upgrade surprises.

### 5. Add the UI widget

Open the relevant `.ui` file in Qt Designer (or hand-edit the XML).
Pick the widget that matches the field type:

| Field type | Widget |
|------------|--------|
| `bool` | `QCheckBox` |
| `int` | `QSpinBox` |
| `float` / `double` | `QDoubleSpinBox` |
| `enum` | `QComboBox` |
| Hex color | `QColorPicker` (custom) |
| Path | `QLineEdit` + browse button (use the existing path-picker pattern) |

Give the widget a stable `objectName` — `SettingsFormBinding` looks
it up by name.

### 6. Bind the widget

In the panel's `*.cpp`, in `refresh()` (or whatever populates the
panel from the model):

```cpp
SettingsFormBinding::loadInto(ui->myNewKeyCheckBox,
                              m_model->collectionConfig->gridLayoutPreferences.myNewKey);
```

And in the panel's constructor (or wherever the connect-back-to-model
lives):

```cpp
SettingsFormBinding::bindCheckbox(
    ui->myNewKeyCheckBox,
    m_model->collectionConfig->gridLayoutPreferences.myNewKey,
    [this](bool) { writeBack(); });
```

The `SETUP_GETTER_*` macros in
[setuputils.h](../../src/utils/app/setuputils.h) cover most boilerplate.

### 7. Decide on propagation

[applysettingsdialog.cpp](../../src/ui/dialogs/settings/core/applysettingsdialog.cpp)
is the **Apply Settings** workflow's gate — when a user picks "apply
to selected collections," this dialog lists which keys are
propagatable. Keys that *should not* propagate (paths, extensions,
parent linkage, launchers — anything tied to a specific collection)
are explicitly excluded.

If your new key **is** propagatable (e.g. an appearance toggle), add
it to the dialog's propagation list. If it isn't (paths, scraper
overrides, hierarchy), it goes in the exclusion list.

### 8. Add the wiki row

[docs/user/Configuration-Reference.md](../user/Configuration-Reference.md)
groups keys by section. Find the right section (here: "Grid layout")
and add a row:

```
| `myNewKey` | bool | `false` | Short, user-facing description of what it controls. |
```

### 9. Test it

| Path | What to check |
|------|---------------|
| Fresh install | Default value lands in the struct and matches the doc. |
| Upgrade | `QSettings::value(key, default)` returns the default for an existing config without the key. |
| Round-trip | Toggle the widget, Save, exit, relaunch — the value sticks. |
| Hand-edit | Hand-set the value in `~/.config/kartend/kartend.cfg`, relaunch — the load picks it up. |
| Apply-Settings (if applicable) | Apply to a second collection and confirm the value propagates (or doesn't, depending on what you decided). |

## Gotchas

- **Float keys** — `settings.value(key, default).toDouble()` returns
  `0.0` for a non-numeric value. Defend at clamp time, not at load
  time.
- **Enum keys** — Persist as `int` (the enum's underlying integer);
  the cast back is `static_cast<MyEnum>(settings.value(...).toInt())`.
  Document the int↔name mapping in the wiki row.
- **List keys** — Persist as a QSettings array (`beginWriteArray`)
  rather than CSV when the entries can contain commas; CSV is fine
  for closed sets like file extensions. See the `datFilePaths` /
  `launcherPresets` patterns for array writes.
- **Secret keys** — Don't add credentials to the regular settings
  flow. They live under `[Scrapers]` and route through the keychain
  layer; see [Keychain](../user/Keychain.md) and
  [scraper-architecture.md](scraper-architecture.md).
- **Wire format vs identifier** — Renaming `kMyKey` is a no-op
  refactor; changing the string `"myKey" → "renamedKey"` breaks
  every existing user's config. If you must rename a wire-format
  string, also handle the legacy read path (see how `datFilePath`
  was preserved when `datFilePaths` was added).

## Related code

| Concern | File |
|---------|------|
| Wire-format key constants | [src/utils/app/settingskeys.h](../../src/utils/app/settingskeys.h) |
| Per-collection leaf clusters | [src/utils/app/collection/](../../src/utils/app/collection/) |
| `GeneralSettings` (global) | [src/utils/app/collection/generalsettings.h](../../src/utils/app/collection/generalsettings.h) |
| `CollectionConfig` (per-collection umbrella) | [src/utils/app/collection/collectionconfig.h](../../src/utils/app/collection/collectionconfig.h) |
| Validation + clamp | The leaf's `*_persistence.cpp` (e.g. [archiveoptions_persistence.cpp](../../src/utils/app/collection/archiveoptions_persistence.cpp)) or [collection/helpers.cpp](../../src/utils/app/collection/helpers.cpp), plus [configvalidation.cpp](../../src/utils/fs/configvalidation.cpp) |
| Load / save | [src/modules/data/settings/](../../src/modules/data/settings/) |
| Apply-settings gate | [src/ui/dialogs/settings/core/applysettingsdialog.cpp](../../src/ui/dialogs/settings/core/applysettingsdialog.cpp) |
| Form binding helpers | [src/ui/dialogs/settings/core/settingsformbinding.h](../../src/ui/dialogs/settings/core/settingsformbinding.h) |
| Setup-getter macros | [src/utils/app/setuputils.h](../../src/utils/app/setuputils.h) |
| Wiki documentation | [docs/user/Configuration-Reference.md](../user/Configuration-Reference.md) |
