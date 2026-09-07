# Vendored third-party libraries

| Library | Version | Upstream | License | Used by |
|---------|---------|----------|---------|---------|
| QtAdvancedDockingSystem | 5.1.1 | https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | LGPL-2.1-or-later (exception, see its LICENSE) | Dockable panels |
| libogg | 1.3.5 | https://github.com/xiph/ogg (tag v1.3.5) | BSD-style (see `libogg/COPYING`) | Ogg bitstream muxing for audio conversion |
| libvorbis | 1.3.7 | https://github.com/xiph/vorbis (tag v1.3.7) | BSD-style (see `libvorbis/COPYING`) | Ogg Vorbis encoder for audio conversion |

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
