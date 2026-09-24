# Onimusha-DualSense-Linux

An unofficial DualSense haptics mod for the PC release of *Onimusha: Way of the Sword*. Download the latest Linux + Proton package from [GitHub Releases](https://github.com/avitretiak/Onimusha-DualSense-linux/releases/latest).

The mod provides action, UI, sound-derived, and adaptive-trigger feedback. It is an approximation, not a reproduction of the PS5 implementation or the game's official effects.

## Architecture

The normal path is deliberately small:

```text
Proton game process
  -> REFramework
     -> reframework/plugins/OnimushaDualSense.dll
        -> game hooks and generated-wave catalog
        -> native DualSense HID output, or four-channel audio output

Optional Linux host process
  -> hidapi -> DualSense HID output
```

Steam Input remains the game's input path. The mod does not replace Steam, the overlay, or controller input. The helper is passive: it accepts haptic reports from the plug-in, writes them to a supported DualSense, and exits when stopped. It does not launch the game.

## Requirements

- A 64-bit Linux installation with Steam and a Proton version that runs the supported PC release.
- A compatible REFramework installation in the game directory. The directory must contain `OnimushaWotS.exe` and `reframework/`.
- One DualSense or DualSense Edge. USB is the recommended connection. Direct output can also use a compatible Bluetooth HID collection; the optional helper currently targets the USB DualSense HID collection.
- A release package containing the native plug-in and Steam launcher. The package does not redistribute game-derived catalogs or WAVs; `prepare-assets.sh` generates and installs them from the user's local game.
- .NET 10 runtime for the packaged framework-dependent asset generator.
- Wine only when native `ree-pak-cli` and `vgmstream-cli` tools are not supplied. The script prefers native tools through `REE_PAK_CLI` and `VGMSTREAM_CLI`.
- For the optional helper, `hidapi` runtime access to the controller. On distributions with device permissions, add the user to the appropriate input group or install a local udev rule instead of running the helper as root.

## Compatibility

The following combination is the tested baseline for this release:

| Game | REFramework | Proton / architecture | Controller path | Status |
| --- | --- | --- | --- | --- |
| Steam AppID `2638890`, build ID `24769601` | Commit `d1461375aee4ec3f313170f8eaad12064eb542d9`, tag `v1.5.9.1`, 507 commits past tag; ABI `1.15.0` | GE-Proton `11-7`, Linux x86_64 | USB DualSense `054c:0ce6`; Steam Input remains enabled | Verified |

Known limits:

- Only x86_64 Linux is packaged and tested.
- The Linux helper supports the USB DualSense HID collection `054c:0ce6`.
  Bluetooth, DualSense Edge, and other controllers are not promised by the
  helper.
- Other game builds, REFramework revisions, Proton versions, architectures,
  or controller transports are unverified until retested.
- The helper needs `hidapi-hidraw`, `libudev`, and user access to the
  controller's hidraw device through the distribution's udev/input policy.
  It does not require root.

The native runtime does not require .NET. The packaged generator requires only
the .NET runtime, not the SDK, PowerShell, or the developer source tree.

## Install on Linux + Proton

1. Exit the game and Steam's game process.
2. Install REFramework in the game directory and confirm that its normal plug-in directory is `<game>/reframework/plugins/`.
3. Extract the release ZIP directly into the directory containing `OnimushaWotS.exe`. The package already has the game-relative layout; do not copy separate `game/` or `host/` trees.
4. Generate the game-derived assets once:

   ```sh
   cd /path/to/game
   sh ./prepare-assets.sh .
   ```

 The script extracts only the required local files, generates the matching
 catalog and WAVs, and installs them under `reframework/data/`. It keeps
 downloaded tools and intermediate metadata under the package's `data/`
 directory. Do not redistribute that directory.
5. In Steam, open the game's **Properties → General → Launch Options** and set this exact line:

   ```text
   sh ./steam-launch.sh %command%
   ```

   Steam expands `%command%` to the normal Proton launch command. The wrapper
   starts the optional HID helper for the game lifetime and stops only the
   instance it started. If the helper cannot start, the game still launches.
6. Leave Steam Input and the rest of the normal Steam settings unchanged.
7. Launch the game from Steam. The REFramework log should report that the
   native plug-in and waveform catalog loaded.

### Optional helper start

The Steam wrapper manages the helper automatically. Run it manually only when
testing outside Steam or when you need to inspect its log:

```sh
sh ./start-hidrelay.sh start
```

Stop that manually started instance after the game exits:

```sh
sh ./start-hidrelay.sh stop
```


## Haptic settings

Edit `reframework/data/OnimushaDualSense.ini`; settings load when the game
starts, so restart after changes. Strength values are linear multipliers from
`0.0` (off) to `1.0` (full).

- `General.MasterStrength` scales all haptics. `General.Enabled=false` disables
  the mod's outputs.
- `General.Footsteps` and `General.RunningFootsteps` scale walking and running
  feedback; both default to `0.5`. `EventHaptics` provides an additional
  per-event multiplier for each action.
- `SoundHaptics` scales sound-derived effects by source family.

The prepackaged INI documents every key and default.

## Update

1. Close the game. The Steam wrapper stops the helper instance it started.
2. Save a copy of the current `reframework/plugins/` files and
   `reframework/data/` directory for rollback.
3. Back up a customized `reframework/data/OnimushaDualSense.ini` separately.
4. Extract the new package directly into the game directory.
5. Restore your customized INI if you backed it up; otherwise keep the new
   release's defaults.
6. Regenerate matching local assets once:

   ```sh
   sh ./prepare-assets.sh .
   ```

7. Keep the Steam launch option
   `sh ./steam-launch.sh %command%` and launch normally.

Keep generated wave data from one release together with its matching native
catalog; do not mix catalogs and wave directories from different releases.

## Uninstall and rollback

Close the game first. Stop any manually started helper, then run this command
from the game directory:

```sh
sh ./start-hidrelay.sh stop 2>/dev/null || true
rm -f \
  reframework/plugins/OnimushaDualSense.dll \
  reframework/plugins/libportaudio64bit.dll \
  reframework/data/OnimushaDualSense.ini \
  reframework/data/onimusha_dualsense_native.bin \
  onimusha_hidrelay \
  prepare-assets.sh \
  start-hidrelay.sh \
  steam-launch.sh \
  RELEASE-METADATA.json \
  LICENSE \
  THIRD_PARTY_NOTICES.txt
if [ -f reframework/data/.onimusha_dualsense_assets ]; then
  rm -rf reframework/data/waves
  rm -f reframework/data/.onimusha_dualsense_assets
fi
rm -rf asset-generator data
```

Remove `reframework/data/waves` only if it was installed by this mod. Do not
remove REFramework, `dinput8.dll`, the game, or Steam files. To roll back,
restore the saved plug-in and matching `data/` directory, then remove the
Steam launch option.

## PR-3 trigger behavior

The native plug-in preserves the PR-3 trigger profiles:

- **Profile 0:** bow charge resistance on R2.
- **Profile 1:** Rift vibration on L2.
- **Profile 3:** Soul absorption vibration override on L2.
- **Profile 2:** low-strength L2 vibration.

A DualSense trigger can carry only one active trigger mode at a time. Rift vibration therefore replaces the Rift resistance that would otherwise be active, and the Soul profile temporarily overrides the base L2 profile. The bow's R2 resistance remains independent. This is a controller protocol limitation, not a configuration error.

## Troubleshooting

- **The plug-in does not load:** confirm the native DLL is under `reframework/plugins/`, REFramework is installed for this game, and the DLL matches the release architecture. Read the REFramework log for `native plugin loaded` and hook warnings.
- **Actions work but no haptics are felt:** connect one controller, close other controller tools, and verify the DualSense is visible to Linux. Try USB first. If direct HID output is unavailable, start the optional helper and look for its startup message.
- **The helper exits immediately:** check that `hidapi` is installed, the helper has permission to open the controller, no second helper is running, and the controller is the supported USB collection. Do not run two helpers against one controller.
- **Sound-derived feedback is missing:** keep `libportaudio64bit.dll` beside the plug-in and preserve the `reframework/data/` catalog and `waves/` files. A four-channel DualSense audio endpoint is required for PCM output; pulse fallback can still provide action feedback.
- **Steam Input or the overlay changes behavior:** restore the normal Steam launch options and controller settings. The mod should not own game input or Steam startup.
- **A trigger effect appears to replace another:** this is expected from the mutually exclusive trigger-mode rule above. Check the active PR-3 profile before changing strength settings.

The native plug-in reports output failures and event names through the REFramework log. The optional helper reports its own errors to the terminal that started it.

## GitHub install package

The full GitHub ZIP includes the native plug-in, runtime dependencies, asset
generator, helper, launch scripts, default INI, licenses, and metadata:

```text
reframework/
  data/OnimushaDualSense.ini
  plugins/
    OnimushaDualSense.dll
    libportaudio64bit.dll
asset-generator/
  OnimushaDualSense.dll
  OnimushaDualSense.deps.json
  OnimushaDualSense.runtimeconfig.json
  defense_haptics.json
onimusha_hidrelay                  (optional fallback)
prepare-assets.sh
start-hidrelay.sh
steam-launch.sh
LICENSE
THIRD_PARTY_NOTICES.txt
RELEASE-METADATA.json
```

Extract this ZIP directly into the game directory, run
`sh ./prepare-assets.sh .` once, then set Steam launch options to
`sh ./steam-launch.sh %command%`.


## Maintainer release

Push a semantic version tag such as `v1.3.2`. GitHub Actions builds the Windows
plug-in, Linux helper, and asset generator, then publishes one versioned ZIP:
`Onimusha-DualSense-Linux-vX.Y.Z.zip`. Generated game assets are never
included. Release notes should state the compatibility baseline and source
commit.

## Development

See [docs/in-process-rewrite.md](docs/in-process-rewrite.md) for the source boundary and known limitations.

## Credits

- Original Windows mod and upstream project: [AInine9 / Onimusha-DualSense](https://github.com/AInine9/Onimusha-DualSense).
- REFramework, PortAudio, and setup-tool licenses are listed in [THIRD_PARTY_NOTICES.txt](distribution/THIRD_PARTY_NOTICES.txt).

## Legal

This project is unofficial and is not affiliated with Capcom, Sony, or the game developers. Source code is available under the [MIT License](LICENSE). Third-party components are listed in [distribution/THIRD_PARTY_NOTICES.txt](distribution/THIRD_PARTY_NOTICES.txt).

Development used substantial AI assistance. See [AI-DISCLOSURE.md](AI-DISCLOSURE.md) for the scope of that assistance and the maintainer's release-review responsibilities.
