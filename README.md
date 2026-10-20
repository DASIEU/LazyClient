![LazyClient](docs/assets/lazyclient-banner.png)

# LazyClient

LazyClient is an all-in-one **gores-focused** fork of [TClient](https://github.com/TaterClient/TClient) and [DDNet](https://ddnet.org/), built for hook-heavy corridors, freeze walls, and the long grind of gores maps.

It is designed for servers like **King of Gores** and **FoxNet**: the prediction, visibility, and recording tools that gores players actually use, without turning the game into something that plays itself.

> [!IMPORTANT]
> LazyClient is **fair play only**. There is no automation, no aim assistance, no hidden information, and no gameplay advantage that a vanilla client could not also use. Every feature is local and client-side.

---

## Why LazyClient?

Most clients are either a general DDNet client with gores settings buried somewhere, or a collection of scripts held together by a config file. LazyClient puts the gores layer first:

- **Gores preset** — one click for the prediction, input, and recording setup most gores players want.
- **Better visibility** — tile outlines for freeze, unfreeze, kill, teleport, and hookable tiles; colored frozen tees; frozen-team HUD; moving tiles in entities.
- **Better prediction** — TClient's improved antiping, negative certainty buffer, stable-direction prediction, fast input, and DDNet antiping controls in one place.
- **Recording that works** — automatic race demo recording, race ghosts, replays, and ghost opacity.
- **Chat triggers** — execute commands on connect/join, auto-replies, vote helpers, and chat filters.
- **Local-first** — no accounts, no KoG API, no telemetry, and no server-side requirements.
- **Config compatible** — reads your existing DDNet and TClient settings and keeps them intact.

LazyClient is built on TClient 10.9.x / DDNet 20.x. It keeps the TClient feature set and adds a dedicated **LazyClient** settings section with five tabs: **Game**, **Visuals**, **Chat triggers**, **Miscellaneous**, and **Info**.

---

## Fair play

LazyClient is intended for servers that allow client-side quality-of-life features. It does **not** include:

- Aimbots or trigger bots
- Automatic movement, hooking, or race completion
- Input automation that plays the game for you
- Map or player information that is hidden by the server
- Any feature that modifies server-side game state

If a server's rules are stricter than LazyClient's feature set, follow the server's rules. You are responsible for the client you use.

---

## Download

Release builds will be published on the [GitHub Releases page](https://github.com/DASIEU/LazyClient/releases).

LazyClient currently targets **Linux first**. Windows builds are planned once the Linux release pipeline is stable.

### Linux build from source

#### Debian / Ubuntu

```sh
sudo apt install build-essential cmake git ninja-build \
  libcurl4-openssl-dev libfreetype-dev libharfbuzz-dev \
  libogg-dev libopus-dev libopusfile-dev libpng-dev \
  libsdl2-dev libssl-dev
```

Optional dependencies:

```sh
# Video recording
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev

# Vulkan backend
sudo apt install libvulkan-dev

# WebSocket support
sudo apt install libwebsockets-dev
```

#### Arch Linux

```sh
sudo pacman -S --needed base-devel cmake git ninja curl freetype2 harfbuzz \
  libogg opus opusfile libpng sdl2 openssl
```

#### Build

```sh
git clone --recursive https://github.com/DASIEU/LazyClient.git
cd LazyClient
git submodule update --init --recursive

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPREFER_BUNDLED_LIBS=ON
cmake --build build -j"$(nproc)"
```

Run the client:

```sh
./build/LazyClient
```

The server target is still built as `DDNet-Server` for compatibility with existing server tooling.

More build options are documented in [`docs/BUILDING.md`](docs/BUILDING.md).

---

## Configuration compatibility

LazyClient deliberately keeps DDNet's save directory name (`DDNet`) so it can share data with an existing DDNet/TClient installation:

| File | Purpose |
| --- | --- |
| `settings_ddnet.cfg` | Existing DDNet settings |
| `settings_tclient.cfg` | Existing TClient settings |
| `settings_lazy.cfg` | New LazyClient settings (`lc_*`) |

Existing `tc_*` variables are not renamed or removed. New LazyClient variables use the `lc_` prefix and are stored in the `LAZY` config domain.

You can open the config files directly from **Settings → LazyClient → Miscellaneous**.

---

## Settings

**Settings → LazyClient** contains:

- **Game** — gores preset, prediction, fast input, antiping, race ghosts, replays, and auto-record.
- **Visuals** — tile outlines, frozen tee colors/HUD, moving tiles, entity visibility, and screen-center helpers.
- **Chat triggers** — on-connect/on-join commands, auto-replies, vote helpers, and chat filtering.
- **Miscellaneous** — config file shortcuts, release links, quality-of-life options, and the status bar.
- **Info** — version information, links, credits, and license.

Only implemented controls are shown. Planned features are tracked in the issue tracker instead of appearing as dead toggles.

---

## Updates

LazyClient checks the latest **GitHub Release** for updates and links directly to the release page. The client does not download or execute arbitrary update payloads without an explicit release artifact.

The long-term plan is a GitHub Releases-based updater with signed artifacts and a stable update manifest. Until then, update checks are informational and releases are installed manually.

---

## Upstream compatibility

LazyClient is a fork, not a replacement for TClient or DDNet:

- `master` tracks upstream TClient.
- `lazy` is the LazyClient development branch.
- Feature work happens on short-lived branches based on `lazy`.

The goal is to keep merging upstream TClient/DDNet changes so LazyClient does not fall behind on protocol, security, or platform fixes.

---

## Credits

LazyClient would not exist without the work of:

- **DDNet** — Dennis Felsing and the DDNet contributors
- **TClient** — Tater and the TClient contributors
- **DDRace** — the DDRace developers
- **Teeworlds** — Magnus Auvinen and contributors
- **King of Gores / FoxNet** — the servers and players that shaped the gores feature set

LazyClient code and design by **DASI**.

---

## License

LazyClient is an altered source version of TClient, which is a fork of DDNet. The original zlib license and third-party notices are preserved in [`license.txt`](license.txt). Additional attribution is in [`NOTICE.md`](NOTICE.md).

LazyClient is not affiliated with or endorsed by DDNet, TClient, King of Gores, or FoxNet.

---

## Disclaimer

This software is provided **as-is**, without warranty of any kind. Use it at your own risk. Server owners may have rules that differ from LazyClient's feature set; follow those rules.
