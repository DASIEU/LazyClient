# LazyClient notices

LazyClient is a gores-focused fork of **TClient**, which is a fork of **DDNet**.
It is an altered source version and is not the original TClient or DDNet
software.

## Upstream projects

- **Teeworlds** — Copyright (C) 2007-2014 Magnus Auvinen and contributors.
  <https://www.teeworlds.com/>
- **DDRace** — Copyright (C) 2010-2011 Shereef Marzouk and contributors.
- **DDNet** — Copyright (C) Dennis Felsing and contributors.
  <https://ddnet.org/>
- **TClient** — Copyright (C) Tater and contributors.
  <https://github.com/TaterClient/TClient>
- **RushieClient** — contributors of the RushieClient project.
  <https://github.com/RushieClient/RushieClient-ddnet>
- **BestClient** — Copyright (C) 2026 BestProject Team.
  <https://github.com/BestProjectTeam/BestClient>

The automatic prediction margin, snap tap, gores mode, and input-mode
behaviour in LazyClient are adapted from BestClient, which is distributed
under the MIT license. The MIT copyright and permission notice is reproduced
below.

```
MIT License

Copyright (c) 2026 BestProject

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The original zlib license text and the licenses for bundled third-party
libraries, fonts, skins, maps, and other assets are preserved in
[`license.txt`](license.txt) and in the individual asset directories.

## Client indicator assets

The client indicator icons are used only to show which compatible client a
player is using. LazyClient tags its own skin locally. BestClient does not
broadcast a local tag, so LazyClient can optionally fetch BestClient's public
player list from `https://150.241.70.188:8779/users.json` to match players on
the current server. That request is used only for the BestClient icon, is not
an account or presence system for LazyClient, and can be disabled with
`lc_bestclient_public_list`.

- `data/lazy/clients/rushie.png` is from **RushieClient**,
  <https://github.com/RushieClient/RushieClient-ddnet>, licensed under
  CC-BY-SA 3.0.
- `data/lazy/clients/bestclient.png` is from **BestClient**,
  <https://github.com/BestProjectTeam/BestClient>, licensed under CC-BY-SA 3.0.

The skin-color client tag follows the DDNet custom-client identification
convention used by RushieClient and other compatible clients.

## LazyClient changes

LazyClient code, branding, and the gores-focused settings layer are
Copyright (C) 2026 DASI.

The LazyClient Lz wordmark, client icon, and banner in `data/lazy/lazyclient.png`,
`data/lazy/banner.png`, and `docs/assets/lazyclient-banner.png` were created for
this project.

## No endorsement

LazyClient is not affiliated with or endorsed by DDNet, TClient, RushieClient,
BestClient, King of Gores, FoxNet, or any server operator. Server rules may be
stricter than LazyClient's feature set; follow the rules of the servers you
play on.

## Fair play

LazyClient does not include aim assistance, hidden information, or server-side
gameplay modification. It only changes local input handling and rendering.
Optional input helpers such as gores mode, snap tap, and input modes are off by
default; server and community rules may restrict them, so use them only where
they are allowed.
