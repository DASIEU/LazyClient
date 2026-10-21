![LazyClient](docs/assets/lazyclient-banner.png)

# LazyClient

LazyClient is an all-in-one **gores-focused** fork of [TClient](https://github.com/TaterClient/TClient) and [DDNet](https://ddnet.org/), built for hook-heavy corridors, freeze walls, and the long grind of gores maps.

LazyClient is designed for servers like **King of Gores** and **FoxNet**: the prediction, input, visibility, and recording tools that gores players actually use, kept local and configurable.

> [!IMPORTANT]
> LazyClient is **fair play only**. There is no aim assistance, hidden information, or server-side gameplay modification. Optional input helpers such as gores mode, snap tap, and input modes are off by default; server and community rules may restrict them, so use them only where they are allowed.

---

## Why LazyClient?

Most clients are either a general DDNet client with gores settings buried somewhere, or a collection of scripts held together by a config file. LazyClient keeps the TClient base and adds a focused gores layer:

- **Input modes** — off, fast, best, saiko, and cloud modes with their own amount and smoothing settings, plus a one-click reset.
- **Gores mode** — holds the gun; left click switches to the hammer, hits, then switches back after a configurable delay (gun → hammer → hit → gun); ignore heavy weapons; dedicated gun-fire key.
- **Antiping** — off, on, or automatic with a configurable ping threshold.
- **Snap tap** — prioritize the last pressed movement direction with a configurable switch delay.
- **Automatic prediction margin** — raise the antiping margin when ping or jitter gets high.
- **Aspect ratio** — enter a width and height instead of using a slider; the game stretches to fill the window, with a 5 second keep/revert prompt.
- **Local-first** — no accounts, no KoG API, no telemetry, and no server-side requirements.
- **Config compatible** — reads your existing DDNet and TClient settings and keeps them intact.

LazyClient is built on TClient 10.9.x / DDNet 20.x. It keeps the TClient feature set and adds a dedicated **LazyClient** settings section with five tabs: **Game**, **Visuals**, **Chat triggers**, **Miscellaneous**, and **Info**.

---

## Fair play

LazyClient is intended for servers that allow client-side features. It does **not** include:

- Aimbots or trigger bots
- Automatic movement, hooking, or race completion
- Hidden map or player information
- Any feature that modifies server-side game state

Optional input helpers (gores mode, snap tap, input modes) are disabled by default. Some servers and communities may consider them disallowed even though they are purely client-side; follow those rules.

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

- **Game** — input modes (fast, best, saiko, cloud), gores mode, antiping modes, automatic prediction margin, snap tap, and aspect ratio.
- **Visuals** — reserved for future LazyClient-only visual options.
- **Chat triggers** — reserved for future LazyClient-only chat options.
- **Miscellaneous** — config file shortcuts and release links.
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
