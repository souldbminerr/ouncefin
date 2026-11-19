# Ouncefin
Jellyfin on Switch 2
Features:
 - Full audio and video support, including music
 - Mouse mode
 - 120hz display support for even 24hz division
 - Hardware decoding via NVDEC on T239
   - Most common formats are supported aside from H.265 (GSV limitation)
   - For the formats that aren't supported, software is used (dav1d/ffmpeg software)
 - High networking performance (Inmemory cache isn't needed, but reccomended)
 - 4K output support for 4K videos.
 - GPU Rendering acceleration for scaling

This is a fork of switchfin, the unmodified Switchfin readme follows

# Switchfin

<img src="scripts/switchfin.svg" alt="icon" height="128" width="128" align="left">

Switchfin is third-party PC player for Jellyfin that provides a native user interface to browse and play movies and series.
<br>

[![build](https://github.com/dragonflylee/switchfin/actions/workflows/build.yaml/badge.svg)](https://github.com/dragonflylee/switchfin/actions/workflows/build.yaml)
[![NS](https://img.shields.io/badge/-Nintendo%20Switch-e4000f?style=flat&logo=Nintendo%20Switch)](https://hb-app.store/switch/Switchfin)
[![PSVita](https://img.shields.io/badge/-PSVita-003791?style=flat&logo=PlayStation)](https://www.rinnegatamante.eu/vitadb/#/info/1258)
[![PS4](https://img.shields.io/badge/-PS4-003791?style=flat&logo=PlayStation)](https://pkg-zone.com/details/SFIN00000)
[![Flathub](https://img.shields.io/flathub/v/fun.dragonfly.switchfin)](https://flathub.org/apps/fun.dragonfly.switchfin)
[![download](https://img.shields.io/github/downloads/dragonflylee/switchfin/total?label=Downloads)](https://github.com/dragonflylee/switchfin/releases/latest)
[![nightly](https://img.shields.io/badge/nightly-build-green)](https://nightly.link/dragonflylee/switchfin/workflows/build.yaml/dev)

**This project is in its early stages so expect bugs.**

## Screenshots

<table>
  <tbody>
    <tr>
      <th>Home</th>
      <th>Library</th>
    </tr>
    <tr>
      <td><img src="images/home.jpg" alt="Home"></td>
      <td><img src="images/library.jpg" alt="Library"></td>
    </tr>
    <tr>
      <th>Search</th>
      <th>Music</th>
    </tr>
    <tr>
      <td><img src="images/search.jpg" alt="Search"></td>
      <td><img src="images/music.jpg" alt="Music"></td>
    </tr>
    <tr>
      <th>Series</th>
      <th>Episode</th>
    </tr>
    <tr>
      <td><img src="images/series.jpg" alt="Series"></td>
      <td><img src="images/episode.jpg" alt="Episode"></td>
    </tr>
  </tbody>
</table>

## Features

### Media Playback
- Browse and play **movies, series, seasons, episodes, music albums, and playlists**
- Live TV with channel guide and program recommendations
- Direct play and server-side **transcoding**, with auto-detection
- Full audio track, subtitle track, and chapter selection
- Based on **MPV Player**
  - Container formats: MKV, MOV, MP4, AVI
  - Video codecs: H.264, H.265, VP8, VP9, AV1
  - Audio codecs: Opus, FLAC, MP3, AAC, AC-3, E-AC-3, TrueHD, DTS, DTS-HD
  - Subtitle codecs: SRT, VTT, SSA/ASS, DVDSUB
  - Hardware-accelerated decoding; fallback to software decoding when needed

### Remote File Browser
- Browse and play media from external sources:
  - **WebDAV** · **HTTP(S)** · **SFTP** · **FTP** · **local filesystem**
- Manage multiple remote sources with add/edit/remove

### Additional Features
- **Danmaku (弹幕)** — integration with [jellyfin-plugin-danmu](https://github.com/cxfksword/jellyfin-plugin-danmu)
- **Download** — save media for offline viewing, with series batch download
- **MirrorPlay** — remote playback control via WebSocket (browse on phone, play on big screen)
- **Dashboard** — monitor server sessions, activities, and device status; restart and rescan libraries
- **Search** — full-text search with suggestions across all media types
- **14 languages** — English, 简体中文, 繁體中文, 日本語, 한국어, Deutsch, Français, Español, Português, Русский, Čeština, Türkçe, Українська, Tiếng Việt
- External drive support on Nintendo Switch via [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs)

## Input Mapping (Playback)

| Gamepad | Keyboard | Description |
|---------|----------|-------------|
| A       | Space    | Play / Pause |
| B       | Esc      | Stop |
| Y       | O        | Toggle OSD |
| X       | F4       | Show Menu |
| R / L   | [ / ]    | Seek forward / backward |
| +       | F1       | Show video profile |
| R stick | F2       | Toggle video quality |
| L stick | F3       | Toggle playback speed |

Keyboard bindings can be customized in Settings.

```json
{
  "setting": {
    "key_last": "pgup",
    "key_next": "pgdn",
    "key_volume_up": "0",
    "key_volume_down": "9",
    "key_danmaku": "d",
    "key_video_profile": "f1",
    "key_video_quality": "f2",
    "key_video_speed": "f3",
    "key_setting": "f4",
    "key_refresh": "f5",
    "key_forward": "]",
    "key_rewind": "[",
    "key_video_osd": "o",
    "key_video_pause": "space"
  }
}
```

## System Requirements

| Platform | Requirement |
|----------|-------------|
| Windows  | Windows 7 or later with DirectX 11.1 support |
| macOS    | Intel or Apple Silicon, macOS 10.15 or later |
| Linux    | x86\_64 / arm64v8 with OpenGL 3.0+, Flatpak recommended |

## FAQ

**Q: Subtitles don't display on Nintendo Switch?**
A: Place a `.ttf` font file at `/switch/Switchfin/subfont.ttf`.

**Q: How do I play media from a WebDAV / SFTP / HTTP server?**
A: Edit `config.json` and add entries under `remotes`:

```json
{
  "remotes": [
    {
      "name": "local",
      "url": "file:///switch"
    },
    {
      "name": "xiaoya",
      "passwd": "guest_Api789",
      "url": "webdav://192.168.1.5:5678/dav",
      "user": "guest"
    },
    {
      "name": "rpi",
      "url": "sftp://pi:raspberry@192.168.1.5/media"
    },
    {
      "name": "rclone",
      "url": "http://192.168.1.5:8000"
    }
  ]
}
```

*Example: using [rclone](https://rclone.org/downloads/) to serve files over HTTP:*

```bash
rclone serve http --addr :8000 --read-only /media/downloads
```

**Q: The app won't open on macOS?**
A: Run the following in Terminal to remove the quarantine attribute:

```bash
sudo xattr -rd com.apple.quarantine /Applications/Switchfin.app
```

## Development

```shell
git clone https://github.com/dragonflylee/switchfin.git --recurse-submodules --shallow-submodules
```

### Nintendo Switch

Set up the [devkitPro environment](https://devkitpro.org/wiki/Getting_Started), then:

```bash
sudo dkp-pacman -S switch-dev switch-glfw switch-libwebp switch-curl switch-libmpv
cmake -B build_switch -DPLATFORM_SWITCH=ON
make -C build_switch Switchfin.nro -j$(nproc)
# Debug with nxlink
nxlink -a <YOUR_IP> -p Switchfin/Switchfin.nro -s Switchfin.nro --args -d -v
```

### Nintendo Switch 2

Requirements: devkitPro + ounce-devkit (libounce + drako3d), and the Switch 2 portlibs (FFmpeg with `*_viddec` decoders, libmpv). Run in msys2 or native linux shell

```bash
source /opt/devkitpro/switch2vars.sh
# first time: configure (Ninja generator is required)
aarch64-none-elf-cmake -S . -B build_switch2 \
  -DPLATFORM_SWITCH2=ON -DUSE_DRAKO3D=ON \
  -DCMAKE_BUILD_TYPE=Release -G Ninja
# incremental build + package (rerun these two after any change)
cmake --build build_switch2 -j8
cmake --build build_switch2 --target Switchfin.nro -j8
# output: build_switch2/Switchfin.nro
```

(`scripts/build-switch2.sh` does the configure + build in one go.)
The borealis patches under `scripts/switch2/borealis/` are already applied
to the in-tree copy at `library/borealis`; when forking borealis, apply
them to the fork instead (see `scripts/switch2/borealis/README.md`).

#### Switch 2 portlibs (FFmpeg + mpv)

The app links static Switch 2 portlibs from `$DEVKITPRO/portlibs/switch2`.
Build FFmpeg first (mpv depends on it). All commands run in msys2 after
`sourcing switch2vars.sh`:

```bash
source /opt/devkitpro/switch2vars.sh
# FFmpeg 7.1.5 + viddec HW decoders (h264/vp9/av1/vp8)
bash scripts/switch2/ffmpeg/build-switch2-ffmpeg.sh
# (same content as a dkp-pacman package: see scripts/switch2/ffmpeg/PKGBUILD)
```

Base Switch 1 portlibs it links against (`switch-zlib`, `switch-bzip2`,
`switch-libass`, `switch-freetype`, `switch-mbedtls`, `switch-libdav1d`)
come from normal `dkp-pacman`; the script overlays only the rebuilt
FFmpeg onto the Switch 2 prefix.

Then libmpv 0.36.0 (static, software render path only — there is no
runtime GPU shader compiler on Switch 2, the app draws video with
`MPV_SW_RENDER`):

```bash
source /opt/devkitpro/switch2vars.sh
meson setup build-mpv mpv-0.36.0 \
  --cross-file scripts/switch2-cross.ini \
  --prefix "$DEVKITPRO/portlibs/switch2" --default-library static \
  -Dlibmpv=true -Dcplayer=false -Dtests=false
meson compile -C build-mpv
meson install -C build-mpv
```

`scripts/switch2-cross.ini` is the meson cross file (`horizon` system,
`cortex-a78c`, Switch 2 pkg-config + libounce include/link flags).

### Linux / macOS / Windows (Desktop)

Ensure `mpv`, `libcurl`, `libwebp`, and their development headers are installed, then:

```bash
cmake -B build -DPLATFORM_DESKTOP=ON
cmake --build build
```

### Windows (MinGW64)

```bash
pacman -S ${MINGW_PACKAGE_PREFIX}-cc ${MINGW_PACKAGE_PREFIX}-ninja ${MINGW_PACKAGE_PREFIX}-cmake
cmake -B build_mingw -G Ninja -DPLATFORM_DESKTOP=ON
cmake --build build_mingw
```

### Debug Mode

```bash
./Switchfin -d -v              # debug logs + overlay
./Switchfin -v -o app.log      # also save logs to a file
```

These flags can be combined:

- `-d` — print more detailed debug logs.
- `-v` — show the on-screen debug overlay (FPS, memory, ...) and save MPV logs to `mpv_<timestamp>.log`.
- `-t` — show MPV's own messages in the terminal.
- `-o <file>` — write the app log to a file (default `switchfin.log`).
- `-version` — print the version and exit.
The overlay can also be toggled in **Settings → Others → Debug**. On Switch, pass arguments via `nxlink` (see above).

If a video shows a black screen or garbled picture, check `mpv_*.log` — it usually reveals an unsupported codec, hardware decode failure, or a server error. Attach it when reporting decoding bugs.

## Acknowledgements

- **@xfangfang** for [wiliwili](https://github.com/xfangfang/wiliwili)
- @devkitpro and switchbrew for [libnx](https://github.com/switchbrew/libnx)
- @natinusala and XITRIX for [borealis](https://github.com/natinusala/borealis)
- @proconsule for [nxmp](https://github.com/proconsule/nxmp)
- @averne for [FFmpeg](https://github.com/averne/FFmpeg) hwaccel backend
- @averne for [mpv](https://github.com/averne/mpv) deko3d backend
