# Movesets

Shipwright includes **Vanilla** and **Modern** without requiring a separate mod. Modern adds a grounded manual jump and a sustained sprint while a button is held; it does not claim unreported sword combos. Select movesets independently for Adult Link and Child Link at **Enhancements → Movesets**; without optional packages, both menus contain only Vanilla and Modern, with Vanilla as the default.

Modern defaults to **Y** for jump and **hold X + movement** for sprint. Y jumps even when a nearby context action is available; those interactions remain on their usual button. Jumping requires a fresh button press, so holding Jump through landing does not trigger another jump. Choose either action's button in **Enhancements → Movesets**; X and Y can also be rebound in **Settings → Controls → Configure Controller** and appear in the Android touch layout. On Android, the touch stick supplies continuous 360-degree movement in Modern; Vanilla keeps its existing keyboard-style touch behavior. Sprint raises the running speed limit only while held—there is no burst dash or dash cooldown.

## Optional native packages

Native packages are optional extensions; Modern is built in. Packages are supported on Windows, Linux, macOS, and Android; Switch, Wii U, and iOS keep the built-in choices only. Install one package per directory under Shipwright's per-user `mods/movesets/` directory:

```text
mods/movesets/example-slash/
├── manifest.json
├── build/libexample_slash.so
└── example-slash.otr  # optional (.otr or .o2r)
```

Example manifest:

```json
{
  "id": "example-slash",
  "name": "Example Slash",
  "ages": ["adult"],
  "libraries": {
    "windows": "build/example_slash.dll",
    "linux": "build/libexample_slash.so",
    "macos": "build/libexample_slash.dylib",
    "android-arm64": "build/libexample_slash.so",
    "default": "build/libexample_slash.so"
  },
  "archive": "example-slash.otr"
}
```

`ages` accepts `adult`, `child`, or both; when omitted, both ages are enabled. The manifest also recognizes `android-arm32`, `android-x86_64`, and `android-x86`. Library/archive paths must resolve inside the package directory; archives are mounted the first time the package is selected and remain mounted for the session. Restart Shipwright after installing or removing packages.

Build the example with:

```sh
cmake -S examples/movesets/example-slash -B /tmp/example-slash-build
cmake --build /tmp/example-slash-build --config Release
```

Copy the resulting shared library into the platform-specific path in `manifest.json` (for example, `build/libexample_slash.so`) before installing the package.

The C ABI v1 header is `soh/include/soh/MovesetAPI.h`. A plugin exports `ShipMoveset_Initialize`, `ShipMoveset_OnPlayerAction`, and optionally `ShipMoveset_Shutdown`. The callback may continue vanilla behavior or consume it, and can request velocity, an animation, sound, built-in dust/spark effects, and melee-hitbox scaling. Native packages execute in-process; only install code from sources you trust. The adult-only example adds a modest hitbox scale and sparkle without consuming the normal sword action.
