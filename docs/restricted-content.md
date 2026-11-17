# GoGram content settings and macOS installation

## Content settings

**Settings → Advanced → Show restricted chats and messages** is enabled by
default and saved per account. It suppresses local chat/message restriction
notices and sensitive-media covers. Turning it off restores upstream display
rules; loaded message views and chat availability are refreshed immediately.

The separate **Bypass copy protection** switch in the video-download queue
controls local copy, save, and forwarding restrictions, including stories.
It is also enabled by default and saved per account.

These settings do not grant membership, change server permissions, recover
missing messages or files, purchase media, or complete age verification.
GoGram can only display data returned by Telegram or already available locally.

## Install a local macOS build

After preparing dependencies and building `out/Telegram.app` using the
[macOS guide](building-mac.md), run:

```sh
python3 tools/install-local-macos.py
```

The installer creates `~/Applications/GoGram Desktop.app` and a `tg` launcher
in `~/.local/bin`. Add that directory to your shell's `PATH` if necessary.
Open **GoGram Desktop** from Finder or Spotlight, or run:

```sh
tg
tg --version
tg --path
tg 'tg://resolve?domain=telegram'
```

Installation stages and ad-hoc signs the bundle, verifies its signature, and
retains a previous GoGram bundle as a timestamped hidden backup. It refuses to
overwrite unrelated applications, commands, or symbolic links. Existing launchers
from the earlier fork name can be upgraded. Quit and reopen the app after updating.

The existing `com.gugram.desktop` bundle identifier and `Telegram Desktop`
account-data directory are retained for compatibility; user-visible branding is
GoGram. The installer does not move account data, replace another Telegram app,
or register competing URL/document associations. Earlier app bundles are left
in place. Upstream binary updates remain disabled for this fork.

The local installer targets macOS 15 or newer and does not produce a notarized,
portable release. Actual runtime requirements also depend on the libraries used
to build the application.
