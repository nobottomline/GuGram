# GuGram chat video downloads

GuGram is based on Telegram Desktop **7.2.9**, upstream commit
`fb2e33209517e1a34637d837bfadb3783f2fd59c`. It is the cross-platform C++/Qt
client. The native Telegram for macOS distributed
through the Mac App Store is a separate codebase and has a different interface.

## Usage

1. Open a chat's menu and select **Download all chat videos**.
2. Choose or create a parent folder in the directory picker. New exports use
   `account_<account ID>/<chat name>_<peer ID>` below that folder. Existing
   exports belonging to this account are reused in their original location.
3. Repeat for other chats. **Chat video downloads** reopens the queue window.
4. Select a row to pause, resume, cancel, retry, or open its destination folder.
   Closing or hiding the queue window keeps transfers running.
5. After restarting GuGram, select the same chat and parent folder again.
   Completed files are verified and reused; remaining videos are downloaded.

**Complete** means history traversal ended and every discovered video was saved.
Errors and unavailable media produce **Needs attention**, with counts in the
window and per-file details in `video-export.json`. Newly arriving messages are
outside the history snapshot; run the same chat again to include them.

Copy protection bypass is enabled by default.
Its switch is in the queue window and persists per account. It changes local
copy/save restrictions. Telegram must still provide the actual file: deleted
media, expired media, inaccessible history, and locked paid previews cannot be
recovered by a local UI bypass. The exporter never purchases media.

## Accounts and chat-list indicators

Each authenticated `Main::Session` owns its own manager, timer, request sender,
native file loaders, and worker pool. `Main::Domain::activate` switches the UI's
active account without destroying other authenticated sessions. No exporter work
is tied to the active window, active account, or a window's reactive lifetime.
Switching accounts or hiding a queue window does not pause or cancel exports;
other accounts can start their own queues concurrently. Limits of two scans and
three file operations apply per account. Logout cancels that account's jobs;
quitting the application ends all queues.

The queue window title includes the account name and stable account ID, and each
row includes the chat's stable peer ID. Identical chat titles and identical peer
IDs in different accounts do not share destination manifests or folder locks.
Folder selection and migration checks run on workers. Matching legacy manifests
are reused only for their own account; renamed chats in the new folder layout
retain their existing destination.

The chat list reserves a fixed space before the name for a video-export badge:

- Down arrow: preparing, scanning, or downloading.
- Green check: the last export completed.
- Red exclamation mark: failed or unavailable media needs attention.
- Pause bars: paused or interrupted work.
- Cross: cancelled work.

Narrow chat lists show the badge in the avatar's upper corner. Online/unread
badges retain their usual locations. Screen-reader row names include the export
state. The queue contains the symbol legend and per-file error details.

When one chat has several destination jobs, an error takes precedence over its
other states; otherwise active work takes precedence over pause/cancellation,
and completion is shown only when no incomplete destination takes precedence.
An attention badge does not stop other videos from downloading.

Last-known badge states persist in each account's encrypted preferences. After
restart, previously active states become paused, never a false running or
completed state. Select the same chat and parent folder to resume using verified
completed files. A green badge records the last export result; it is not a
continuous audit of files moved or deleted outside the application.

On upgrade, a worker also reads matching `video-export.json` receipts from the
last selected parent folder, including legacy and account-scoped subfolders.
This restores older export markers without re-downloading media. It imports only
this account's receipts, maps unfinished work to pause, and never replaces an
existing cache entry or a state set by a live job. Receipts in other parent
folders become known when those chats/folders are selected again. Import does
not start network work.

## Design and invariants

- The session owns the manager. Exports outlive chat navigation and the queue
  window. Logout and session destruction cancel requests before clearing data.
  Active exports prevent automatic app suspension; the display can still sleep.
- Normal history requests cover ordinary video attachments, video documents,
  video messages, albums, and available paid-media documents. Migrated group
  history is traversed separately. The exporter does not depend on loaded UI
  messages or a takeout session, which the standard singleton exporter uses.
- Requests paginate by decreasing message ID, continue until an empty page, and
  reject non-advancing responses. The first page supplies each source's snapshot
  upper bound. Two scans and three file operations run concurrently at most.
  Downloads begin while history is still being discovered.
- The native MTProto loader retains Telegram's CDN, file-reference refresh,
  reconnection, and transfer scheduling. File hashing, duplicate copies,
  directory preparation, and manifest writes run on a bounded worker pool.
  Native download chunk writes use Telegram's existing loader implementation.
- A video occurrence is identified by peer, message and document IDs. Every
  occurrence gets a separate filename, even when another message references the
  same document. Concurrent transfers of an identical document are serialized;
  completed files and existing fully downloaded document locations are reused.
  Different document IDs are not assumed to contain identical bytes.
- Network downloads use unique `.partial` filenames. Final publication checks
  the exact server-advertised size and uses a same-directory rename. Copies use
  `QSaveFile`, check size and SHA-256, and commit only after the entire file was
  copied. Existing unrecognized destination files are never overwritten.
- A completed receipt containing SHA-256 is atomically written before publishing
  the final file. This makes a crash between publication and the UI callback
  recoverable. Restart verification requires both the final file and matching
  size/hash. Damaged recognized results are preserved with a `.corrupt` suffix.
- Manifests belong to one account and chat. IDs and sizes are decimal strings,
  avoiding JSON floating-point precision loss. Relative filename validation
  rejects traversal, symbolic links, and Windows drive-relative names. A folder
  lock prevents simultaneous exporters from sharing the same destination.
- Flood waits respect the server's delay. Other history/download failures have
  bounded retries. A failed video does not stop downloads of other videos.
- Pause/cancel retains completed files. An in-flight video restarts from the
  beginning on resume. A process crash can leave a unique `.partial` file;
  the exporter never treats such a file as completed.

## Existing customization migration

Copy/save protection controls and local deleted-message display were ported to
the updated upstream implementation.

Upstream now uses all 64 message-flag bits. The old `DeletedByOther` bit conflicted
with `HasUnreadPollVote`; deleted-message display now uses a separate field.
Session settings version 5 appends custom flags after the new upstream fields
and reads the older custom version 3/4 layout before those fields.

## Verification

The complete macOS ARM64 Debug target compiled and linked successfully. The
application bundle reports 7.2.9 and has a verified local ad-hoc signature. Native
startup reached the welcome/login screen without authenticating an account.
The queue's authenticated runtime behavior has not yet been qualified.

The standalone file tests exercise real filesystem behavior: duplicate copies,
SHA-256 verification, same-size corruption, truncation, interrupted/cancelled
copies, atomic publication, recovery between receipt and publication, collisions,
64-bit IDs, filename sanitization, long Unicode filenames, mismatched accounts,
invalid manifests, traversal, and symbolic links.

Additional tests exercise two account namespaces with identical chat IDs and
titles, simultaneous folder locks, rejection of another account's manifest,
legacy-folder reuse, renamed chats, independent status caches, interrupted-state
recovery, status precedence across multiple destinations, and production badge
rendering at several sizes without relying on color. These are local filesystem,
state and rendering checks, not authenticated MTProto account-switch tests.

```sh
cmake -S tests/video_export -B out/video-export-tests \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build out/video-export-tests --parallel 2
ctest --test-dir out/video-export-tests --output-on-failure
```

Account-level qualification still requires an authenticated small chat: compare
the exported count to the visible video messages, include a repeated document,
pause/resume during a transfer, navigate to a second chat, close the queue window,
restart, and retry using the same destination. Protected/paid media and Windows
must be qualified separately; filesystem tests do not establish those outcomes.

## Local macOS build

The runnable artifact is `out/Telegram.app`. This is a local ARM64 Debug build,
using the installed Homebrew Qt 6.11.2 and FFmpeg 9.0.2 shared libraries, and the
prepared sibling `Libraries` tree. Those shared libraries require macOS 15 or
newer. The bundle is intended for this machine; it is not a notarized, portable
distribution, a Windows build, or an Intel build. Automatic upstream binary
updates are disabled so they cannot replace the custom functionality.

The existing configured `out` tree includes dependency bridges in `deps-pc` and
`deps-cmake` for the prepared minizip, rnnoise, tg_owt and tde2e libraries.
`Libraries/tlottie` is pinned to `31f1b542f8` and was built as an ARM64 C-API
static library. Rebuild this configured checkout with:

```sh
env -u QT \
  PKG_CONFIG_PATH="$PWD/out/deps-pc:$PWD/../Libraries/local/lib/pkgconfig" \
  CLANG_MODULE_CACHE_PATH=/tmp/tdesktop-clang-module-cache \
  SWIFT_MODULECACHE_PATH=/tmp/tdesktop-swift-module-cache \
  cmake -S . -B out -DCMAKE_BUILD_TYPE=Debug
cmake --build out --target Telegram --parallel 8
codesign --force --sign - \
  --entitlements Telegram/Telegram/Telegram.entitlements out/Telegram.app
codesign --verify --strict out/Telegram.app
```

API credentials remain in the existing private CMake cache and are not recorded
in this document. A clean machine must prepare dependencies and supply its own
Telegram API credentials using the upstream build procedure.
