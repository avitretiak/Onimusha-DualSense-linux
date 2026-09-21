# Native in-process architecture

## Decision

The supported runtime is one Windows x64 REFramework plug-in loaded in the game process. Linux is the host environment for Proton; it is not a second game runtime.

```text
Proton game process
  -> REFramework
     -> native/reframework/OnimushaDualSense.cpp
        -> OnimushaDualSense.dll
           -> game hooks and event queue
           -> generated-wave catalog and PCM mixer
           -> Windows DualSense HID output
           -> optional local report channel to the Linux helper

Linux host (optional)
  -> native/hidrelay/onimusha_hidrelay
     -> hidapi/hidraw -> DualSense
```

The native DLL owns event interpretation, trigger profiles, waveform selection, report construction, output priority, and release-on-shutdown behavior. The helper has no game knowledge: it receives fixed-size reports, opens the supported DualSense HID collection, writes the reports, and closes the device when the session ends.

Steam Input remains the game's input path and remains under Steam's control. The plug-in does not replace the overlay, synthesize game input, or require a modified Steam launch command.

## Source boundaries

| Area | Responsibility | Runtime location |
| --- | --- | --- |
| `native/reframework/` | REFramework ABI entry point, hooks, event queue, trigger report packing, direct HID output, generated-wave playback, and fallback selection | Windows x64 DLL in `<game>/reframework/plugins/` |
| `native/hidrelay/` | Small passive C11 `hidapi` host helper for report forwarding when direct output is unavailable | Linux host, outside the game directory |
| `OnimushaDualSense/` | Developer-only game-data extraction, wave preparation, catalog generation, and behavior-oracle tooling | Build machine only |
| `distribution/` | Release assets, licenses, notices, and generated catalog inputs selected for distribution | Package assembly only |
| `tools/` | Package assembly and optional helper convenience text | Build or release machine |

The native plug-in is the canonical product. The managed developer project must not be presented as an installer, game launcher, or runtime dependency. Game-derived source audio and extraction outputs remain local and are not redistributed.

## Runtime flow

1. REFramework loads `OnimushaDualSense.dll` from `reframework/plugins/`.
2. On the first present callback, the plug-in locates `reframework/data/onimusha_dualsense_native.bin`, validates its version, and maps the generated wave catalog.
3. The plug-in installs hooks for lifecycle/player state, combat, movement, UI, sound requests, and adaptive triggers. Hooks only enqueue feedback or update trigger state; they do not synthesize controller input.
4. For sound events, the catalog validates the event and source object before selecting a generated wave. The mixer uses the DualSense four-channel audio endpoint when available.
5. For action/UI events, the mixer is preferred. If PCM output is unavailable, the plug-in emits a report through direct Windows HID. If that output cannot open under Proton, it sends the same report to the optional Linux helper. The helper is passive and does not choose effects.
6. On shutdown or output failure, the active report is cleared before handles are released.

The output order is intentional: game events and report semantics stay in the Windows DLL; the Linux process is only a transport to a host HID device. No helper is needed when direct output works.

## Optional helper lifecycle

Build the helper with `native/hidrelay/CMakeLists.txt` and install `hidapi` runtime access on the Linux host. Start one helper process before the game only when the REFramework log indicates that direct HID output is unavailable:

```sh
/path/to/onimusha_hidrelay
```

Keep it running while the game is running and stop it with `Ctrl-C` after the game exits. The packaged plug-in/helper pair uses local endpoint `28766`; leave `ONIMUSHA_HIDRELAY_PORT` unset unless a matching producer is configured. The helper accepts one report producer and one supported USB DualSense at a time. It does not launch Proton, alter Steam settings, own input, or persist state. A shell launcher may be shipped as optional text; invoking it with `sh ./start-hidrelay.sh` is equivalent to running the helper directly and is the manual fallback when executable permissions are missing.

A helper failure must not prevent the game from starting. The native DLL continues to attempt direct output and records output failures in the REFramework log.

## Install, update, and rollback boundary

The game-side files are deliberately limited to:

```text
<game>/reframework/plugins/OnimushaDualSense.dll
<game>/reframework/plugins/libportaudio64bit.dll
<game>/reframework/data/onimusha_dualsense_native.bin
<game>/reframework/data/waves/*.wav
```

An optional helper and launcher stay on the Linux host. Installation copies those files without changing the normal Steam launch command. Update replaces the plug-in and its matching catalog/waves as one set. Rollback restores the saved set or removes only those mod-owned files; REFramework, the game, Steam Input, and Steam files remain outside the mod's ownership boundary.

## Native feedback map

The native hook map covers the following observed game events:

| Feedback | Hook or state transition |
| --- | --- |
| Footsteps and running | `EPVExpertFootLandingCustom.play` plus player move type |
| Dodge and landing | `CharacterBase.evBaseActionEnter` plus action-state transition |
| Attack and hit | `cPlayerCharacterEntity.evAttackCollision`, `onHitAttackPostProcess` |
| Guard and perfect dodge | `cPlayerGuardController.addDamage`, `cPlayerJustDodgeSupporter.executeSuccessJustDodgeAction` |
| Heal and damage | `cPlayerCharacterEntity.onAddHealth` with kind/amount gates |
| Soul, pickup, lock-on, and power | corresponding player support/entity hooks |
| Finisher | `deadHeatActionImpactNotice`, `attackBreakImpactNotice` |
| UI select, decide, and cancel | `ace.GUIBase` trigger-sound methods |
| Bow charge | `startStrongShot`, `requestEffectStrongShot` |
| Adaptive trigger state | `AdaptiveTriggerManager.onAdaptiveTrigger` |
| Generated sound haptics | `soundlib.SoundManager.postRequestInfo` plus catalog source checks |

## PR-3 trigger contract

The trigger payload retains the PR-3 mapping and ten-zone packing:

- Profile `0` is bow/R2 resistance.
- Profile `1` is Rift/L2 vibration.
- Profile `3` is the Soul/L2 vibration override.
- Profile `2` is the low-strength L2 vibration profile.

One trigger can carry only one active trigger mode. Consequently, Rift vibration replaces the resistance that would otherwise be active on L2, and the Soul profile temporarily overrides the base L2 profile. R2 bow resistance remains independent. This mutually exclusive-mode limitation is part of the controller protocol and must remain visible in user documentation.

## Current limitations

- The plug-in is Windows x64 code and depends on a compatible REFramework ABI and game version.
- Native HID support is implemented for DualSense output collections with the supported vendor/product identifiers. Proton device visibility and permissions can vary by distribution and connection type.
- The optional helper currently targets the USB DualSense HID collection through `hidapi`; it is not a general controller daemon and does not promise Bluetooth coverage.
- PCM feedback requires the bundled PortAudio runtime and a four-channel DualSense audio endpoint. Action feedback can fall back to report pulses when PCM is unavailable.
- Generated catalogs and waves are versioned build artifacts and must remain matched. Game-derived source audio is not included in releases.
- Hook names and managed object layouts are game-specific. A game update can require a hook or catalog refresh even when the plug-in still loads.
- The native path has no end-user configuration surface for changing event routing. Trigger-mode exclusivity cannot be removed by changing a setting.

## Acceptance evidence

A release is considered aligned with this boundary when:

- the game starts with its normal Steam launch settings;
- REFramework logs the native plug-in and matching catalog as loaded;
- direct output is attempted before the optional helper;
- a helper can be started and stopped independently without owning game input;
- a release report clears the controller on shutdown; and
- PR-3 profile 0/1/2/3 behavior and the mutually exclusive trigger limitation remain observable.
