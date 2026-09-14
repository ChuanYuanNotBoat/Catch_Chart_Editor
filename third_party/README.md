# Third-party dependencies

| Library | Version | Source form | Upstream | License / provenance | Used by |
|---------|---------|-------------|----------|----------------------|---------|
| AutoTimingCore | `90f7a9529e1bcdb15475bfa9db5d873b0a38f1e2` | Git submodule | https://github.com/ChuanYuanNotBoat/AutoTimingCore | Legacy license not confirmed; see `AutoTimingCore/ATTRIBUTION.md` and `docs/AUTOTIMING_VENDORING.md` | Legacy BPM detection and AutoTiming 2 analysis |
| QtAdvancedDockingSystem | 5.1.1 | Vendored source | https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | LGPL-2.1-or-later (exception, see its LICENSE) | Dockable panels |
| libogg | 1.3.5 | Vendored source | https://github.com/xiph/ogg (tag v1.3.5) | BSD-style (see `libogg/COPYING`) | Ogg bitstream muxing for audio conversion |
| libvorbis | 1.3.7 | Vendored source | https://github.com/xiph/vorbis (tag v1.3.7) | BSD-style (see `libvorbis/COPYING`) | Ogg Vorbis encoder for audio conversion |

## AutoTimingCore

Initialize the pinned submodule after cloning:

```bash
git submodule update --init --recursive
```

CCE consumes the upstream `AutoTimingCore::legacy` and
`AutoTimingCore::core` CMake targets directly. See
`docs/AUTOTIMING_VENDORING.md` for the pin, update procedure, integration
boundary, and provenance caveat.
Its `ATTRIBUTION.md` is copied to the runtime and installed package under
`notices/AutoTimingCore`.

## libogg / libvorbis

Vendored for the "convert non-OGG chart music to OGG" feature
(`src/audio/AudioConverter.{h,cpp}`). The upstream trees were pruned to the
minimum required for a static link (`include/`, `src/` or `lib/`, plus
`AUTHORS`/`COPYING`/`README`); each directory carries its own minimal
`CMakeLists.txt` and is included from the root `CMakeLists.txt` with
`EXCLUDE_FROM_ALL`.

License compliance: both libraries are distributed with the application and
their `COPYING` files are installed to
`share/<app>/licenses/libogg` and `share/<app>/licenses/libvorbis`
by the root `CMakeLists.txt` install rules, mirroring how the
QtAdvancedDockingSystem license is handled.
