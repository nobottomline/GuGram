# Restricted content and local macOS installation

## Behavior

**Settings > Advanced > Show restricted chats and messages** is enabled by
default. The preference is independent of copy-protection bypass and persists
in the account's existing encrypted preferences store.

When enabled, the shared `UnavailableReason::Compute` predicate returns no local
display restriction for peers or messages. `IgnoreSensitiveMark` also suppresses
the local sensitive-media cover. The original restriction metadata is retained.
Turning the setting off restores upstream matching against platform/reason rules
and server-provided ignored reasons. Loaded message views are refreshed and peer
availability updates are emitted when the preference changes.

This permits opening chats whose restriction metadata would otherwise make the
client close them or show a notice instead of their contents. It does not grant
membership, change server permissions, undo deletion, purchase media, complete
age verification, or suppress actual history/file API failures. The client can
display only data that Telegram returns or that remains available locally.

The mobile GuGram checkout has a separate default-on **Show Restricted Chats &
Messages** switch under **Core Features**. Its peer and message restriction
helpers consult the same GuGram preference. Reopen a mobile chat after changing
the switch. The mobile source change requires rebuilding the iPhone app; the
macOS installation does not update an installed iPhone application.

## Launching

The installed application is `~/Applications/GuGram Desktop.app`. Open it from
Finder's Applications folder in the home directory, or search for **GuGram
Desktop** in Spotlight. The terminal command is `tg`:

```sh
tg
tg --version
tg --path
tg 'tg://resolve?domain=telegram'
```

The launcher lives at `~/.local/bin/tg`; that directory is already in this
machine's login-shell PATH. It invokes the installed app by its absolute path,
so another Telegram application cannot be selected accidentally.

The installed bundle uses the distinct `com.gugram.desktop` identifier and
GuGram Desktop display name. It retains the fork's compiled `Telegram Desktop`
data-directory name. The installer does not move, copy, or delete account data,
replace `/Applications/Telegram.app`, or register competing Telegram URL/document
associations. Automatic upstream binary updates remain disabled for the fork.

## Reinstalling after a build

```sh
cmake --build out --target Telegram --parallel 8
python3 tools/install-local-macos.py
```

The installer validates and signs a staged bundle before moving it into place.
An existing GuGram installation is retained in a timestamped hidden `.previous`
bundle beside the new app. It refuses to overwrite another application, an
unrelated `tg` command, or symbolic links at those destinations. Installation
does not terminate an existing client; quit and reopen GuGram after updating if
an older GuGram process is already running.

This is a local ARM64 Debug build for macOS 15 or newer, using the Homebrew
libraries installed on this machine. It is not a portable or notarized release.

## Verification

- The complete macOS Debug target compiled and linked successfully.
- The installed bundle passed `codesign --verify --strict`.
- The installed app reached the welcome screen using an isolated temporary
  profile. The normal account profile was not opened for this check.
- `tg` launched that installed bundle with another isolated profile; its log
  confirmed the requested working directory. `tg --version`, `tg --path`, and
  command discovery from a fresh login shell passed.
- Existing video-export filesystem tests still passed.
- Opening a real restricted bot, cache availability, and server-side history
  availability remain account-level checks. Startup is not proof of those cases.
