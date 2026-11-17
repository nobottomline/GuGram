# GoGram Desktop

GoGram is an independent, open-source fork of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), based on version **7.2.9**. It uses the Telegram API and MTProto and is not affiliated with Telegram.

## Features

- **Background video downloads:** download all available videos from a chat, with pause, resume, retry, verified file reuse, and a persistent export manifest.
- **Multiple accounts and queues:** downloads continue while you switch chats or accounts. Chat-list badges show progress, completion, and errors.
- **Content controls:** show restricted chats and messages, hide sensitive-media covers, and optionally bypass local copy, save, and forwarding restrictions.
- **Deleted-message indicators:** keep already loaded incoming messages visible when a deletion update arrives, marked as deleted. This does not recover messages from the server or provide a permanent archive.
- **Experimental WEB proxy:** inherited HTTPS/WebView transport with browser fallback; requires a compatible relay server.

Get started from a chat's **Download all chat videos** menu. The content-display switch is under **Settings → Advanced**; the copy-protection switch is in the video-download queue.

GoGram can only display or download content that Telegram returns or that is already available locally. Server permissions, unavailable files, and paid-media access still apply.

Details: [video downloads](docs/video-downloads.md) · [content settings and macOS installation](docs/restricted-content.md) · [WEB proxy design](docs/web-proxy-plan.md).

## Build

```sh
git clone --recursive https://github.com/nobottomline/GoGram.git tdesktop
cd tdesktop
```

Provide your own [Telegram API credentials](docs/api_credentials.md), then follow the platform instructions:

- [macOS](docs/building-mac.md)
- [Windows](docs/building-win.md)
- [Linux using Docker](docs/building-linux.md)

The fork has been built locally on macOS ARM64. Ready-to-install releases are not provided yet; Windows/Linux builds and authenticated video-export scenarios still need qualification. The local macOS installer requires a built `out/Telegram.app` and keeps the existing account-data location. See the [installation guide](docs/restricted-content.md).

## Contributing

Bug reports and focused pull requests are welcome in [this repository](https://github.com/nobottomline/GoGram). Include your platform, build version, steps to reproduce, and relevant redacted diagnostics. See [contributing](.github/CONTRIBUTING.md).

## License

[GPLv3 with the upstream OpenSSL exception](LICENSE). Original Telegram Desktop copyright notices and third-party licenses are retained. Thanks to the Telegram Desktop contributors and the libraries below.

## Third-party

* Qt 6 ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) and Qt 5.15 ([LGPL](http://doc.qt.io/qt-5/lgpl.html)) slightly patched
* OpenSSL 3.2.1 ([Apache License 2.0](https://openssl-library.org/source/license/apache-license-2.0.txt))
* WebRTC ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
* zlib ([zlib License](http://www.zlib.net/zlib_license.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Vazirmatn font ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
* QR Code generator ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
* CMake ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
* Hunspell ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
* Ada ([Apache License 2.0](https://github.com/ada-url/ada/blob/main/LICENSE-APACHE))

