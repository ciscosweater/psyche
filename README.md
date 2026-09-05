# psyche

Linux app (C++17, Qt 6/QML) that searches Hubcap, imports Lua/ZIP packages, and merges them into SLSsteam's `config.yaml`. It does not install games, Steam plugins, or execute Lua. Lua is scanned only. YAML edits keep comments and write a backup first. Default renderer is software (`QT_QUICK_BACKEND`).

## Install

[Releases](https://github.com/ciscosweater/psyche/releases) has a `*-setup.zip`. Extract it and run `./install.sh`, then `~/.local/share/psyche/bin/psyche`. Or run `./psyche` from the extracted directory — the Qt runtime has to stay next to the binary. Linux x86_64, glibc 2.41+ (Debian 13 / similar).

## Build

CMake ≥ 3.21, C++17, Qt ≥ 6.5 (Quick, Quick Controls 2, Concurrent, Network), libarchive, yaml-cpp.

```sh
cmake -S . -B build
cmake --build build -j
./build/psyche
```

## CLI

Any argument starts the CLI (no display). Nothing is written without `--apply`.

```sh
./build/psyche --search "Game name"
./build/psyche --appid 620
./build/psyche --zip package.zip --apply --destination /path/to/SLSsteam
./build/psyche --help
```

Search and AppID downloads use Hubcap quota. Local ZIPs do not.

## Hubcap

Set the key in Settings, or export `PSYCHE_HUBCAP_API_KEY`. If neither is set, psyche reads ASSella's `morrenus_api_key` from `$XDG_CONFIG_HOME/Tachibana Labs/ACCELA.conf` (read-only). A saved key goes unencrypted into `~/.local/share/psyche/settings.json` (`0600` file, `0700` directory). Check **Remember on this computer** only if you want that.

## License

MIT. The ZIP ships Qt and other runtime libraries (see `licenses/THIRD_PARTY.md` in the bundle). Pixelify Sans is SIL OFL (`qml/fonts/OFL.txt`).

I used AI while writing this. The code can be wrong in ways that look fine — bugs, bad edges, leftover junk. Read it before you trust it, especially around `config.yaml` and API keys. MIT, no warranty.
