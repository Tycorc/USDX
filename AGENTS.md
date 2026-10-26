# AGENTS.md — Tycorc/USDX

This fork of UltraStar-Deluxe/USDX carries optional libmpv video playback for the
owner's own karaoke use. Upstream declined the feature (UltraStar-Deluxe/USDX#1462):
never open a pull request or issue against upstream. Changes land by pull request
into this fork's `master`, which follows upstream `master`. Build and platform notes
are in `COMPILING.md`.

- `test/VideoSyncTest.pas` covers the audio-clock sync controller: seek-back during
  the 400 ms screen fade, seek blanking, end of file against container duration.
  Build and run it as `.github/workflows/video.yml` does; on a flakelab box fpc comes
  from `nix shell nixpkgs#fpc`.
- Native Windows verification runs from an ignored `build/windows-test-<shortsha>/`
  root that holds its own scripts, songs, fixtures, libmpv runtime and CI artifacts.
  A new root starts from the previous root's `tools/` with the commit and run id
  updated; Windows reads WSL symlinks, so a root can link shared inputs. Only one
  sample song has a video (25 fps, stream length equal to container duration), so the
  native run does not cover low frame rates or audio that outlasts the video.
