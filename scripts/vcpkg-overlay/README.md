# vcpkg-overlay (embedded)

This directory vendors the subset of the
[`stdware/vcpkg-overlay`](https://github.com/stdware/vcpkg-overlay) overlay
that the synthrt project actually consumes, so CI and local manifest-mode
builds are fully self-contained: no `git clone` of the overlay is needed.

- `ports/` — custom ports: `qmsetup`, `stdcorelib`, `cpp-pinyin`,
  `syscmdline`, `ffmpeg-builds` (the full dependency closure of
  `scripts/vcpkg-manifest/vcpkg.json`).
- `triplets/` — the per-platform triplets used by CI/local
  (`x64-windows`, `x64-linux`, `x64-osx`, `arm64-osx`), including the
  `blake3`-static special case on Windows and `-g` debug info flags.

## Layout wiring

`scripts/vcpkg-manifest/vcpkg.json` points its overlay config here:

```json
"configuration": {
  "overlay-ports":    ["../vcpkg-overlay/ports"],
  "overlay-triplets": ["../vcpkg-overlay/triplets"]
}
```

## Syncing with upstream

The embedded ports mirror the upstream `stdware/vcpkg-overlay` (kept in sync
with the `ds-editor-lite` environment). To refresh them:

```sh
git -C <lite checkout>/scripts/vcpkg pull
# or clone: git clone --depth 1 https://github.com/stdware/vcpkg-overlay.git /tmp/vcpkg-overlay

cp -r /tmp/vcpkg-overlay/ports/{qmsetup,stdcorelib,cpp-pinyin,syscmdline,ffmpeg-builds} scripts/vcpkg-overlay/ports/
cp    /tmp/vcpkg-overlay/triplets/{x64-windows,x64-linux,x64-osx,arm64-osx}.cmake scripts/vcpkg-overlay/triplets/
git status   # review the diff
git add scripts/vcpkg-overlay
git commit -m "chore(vcpkg): sync overlay ports and triplets from upstream"
```
