# AGENTS.md — USDX fork

This UltraStar Deluxe fork carries optional libmpv video playback for personal
karaoke use. Build and platform notes live in COMPILING.md.

- Never open a PR or issue against upstream for this feature; it was declined.
  Changes land by PR into this fork's master, which follows upstream master.
  Always specify this fork when using gh. Preserve its configured merge method,
  hooks and required checks.
- Read [video validation](docs/agents/video-validation.md) before playback,
  audio-clock sync, libmpv or Windows work. Verify the intended platform and state
  the native fixture limits; passing one sample is not coverage for every stream.
- Keep optional playback behavior optional and preserve existing build/platform
  support. Test through the existing video workflow and isolated native fixture
  root; never commit songs, proprietary runtime inputs or test artifacts.
- Keep private addresses, personal/employer details and credentials out of this
  public repository. Use the repository-local identity or a GitHub noreply email.

Keep repository-wide instructions here; put task-specific guidance in `docs/agents/`
and link it from this file. README owns human setup and architecture; plans hold open work only.
