OpenELP, an Open Source EchoLink Proxy
======================================

> [!IMPORTANT]
> **About this repository: criede/openelp**
>
> This repository is an automatically tracking fork of the original OpenELP
> project, [cottsay/openelp](https://github.com/cottsay/openelp). A scheduled
> workflow merges upstream's `main` into this fork's `main` every day, and
> every published upstream release is picked up automatically as well.
>
> Its purpose is building and publishing packages (`.deb`) of OpenELP.
> OpenELP itself is not developed here; local changes are limited to what the
> packaging needs and to the security changes listed below. Please report bugs
> and feature requests for OpenELP itself to
> [the upstream project](https://github.com/cottsay/openelp/issues).
>
> **Security changes in this fork:** besides the packaging, this fork adds
> security fixes that mitigate vulnerabilities in SvxLink for SvxLink nodes
> that reach the EchoLink network only through this proxy. The proxy drops the
> offending packets from the Internet before forwarding them to its client:
>
> * **Malformed NDATA packets** (UDP 5198) — CVE-2026-73152 /
>   GHSA-2m63-p9h9-p5rf: SvxLink parses EchoLink info/chat packets without
>   validating their length or terminator (out-of-bounds read, crash, memory
>   disclosure). Dropped: non-audio packets shorter than 7 bytes and NDATA
>   packets without a NUL terminator after the start of their text.
> * **RTCP SDES packets with an unsafe callsign** (UDP 5199) —
>   GHSA-6w9x-486p-ggrj: SvxLink uses the remote callsign from SDES packets
>   unsanitized, and ModuleEchoLink inserts it into TCL event strings (remote
>   TCL command execution on the SvxLink host) and fixed-size buffers (stack
>   buffer overflow in APRS status reporting). Dropped: packets whose callsign
>   contains characters other than `A-Z a-z 0-9 - / * _` or is longer than 32
>   characters — the same rules as SvxLink's own fix.
>
> Not reachable through OpenELP in the first place: the receive buffer
> overflow in SvxLink's EchoLink proxy client (GHSA-6w9x-486p-ggrj), which
> needs more than 16 kB in one proxy message; OpenELP sends at most 4 kB.
>
> Hardening of OpenELP itself: a 10 s timeout for client login (an idle
> connection could otherwise block the proxy slot), a fixed heap overflow
> with a long `RegistrationName`, and a systemd sandbox running the proxy as
> the unprivileged user `openelp`.
>
> These mitigations are a stop-gap, **not a replacement for updating SvxLink
> to 26.05.2 or newer**, which fixes the vulnerabilities themselves. They are
> part of the `main` (daily) packages only; the `release` packages are built
> from unmodified upstream source (the systemd sandbox is part of both).
>
> **Packages:** `openelp` for Debian and Raspberry Pi OS:
>
> * `arm64` — Raspberry Pi OS 64-bit, Debian 12 (bookworm) and 13 (trixie)
> * `armhf` — Raspberry Pi OS 32-bit on every Raspberry Pi model (incl.
>   Pi 1/Zero/Zero W), Debian armhf; bookworm and trixie
> * `amd64` — Intel/AMD PCs and servers, 64-bit; bookworm and trixie
> * `i386` — Intel/AMD PCs, 32-bit; bookworm only (Debian 13 no longer
>   supports installing i386 systems)
>
> **Two ways to get them:**
>
> * **APT repository** (recommended, updates via `apt upgrade`):
>   <https://criede.github.io/openelp/> — see the
>   [quickstart](https://criede.github.io/openelp/#quickstart). Component
>   `release`: the official upstream releases (0.9.3 and newer), built from
>   the unmodified upstream source and kept permanently; component `main`:
>   daily builds of this fork's `main`, including the security changes above.
> * **GitHub Releases**: <https://github.com/criede/openelp/releases> —
>   `release-<tag>` holds the packages of an upstream release,
>   `daily-<version>` prereleases hold the daily builds; download the `.deb`
>   file and install it with `sudo apt install ./openelp_*.deb`.
>
> Details on how the packages are built and published:
> [UPSTREAM-SYNC.MD](.github/UPSTREAM-SYNC.MD).

OpenELP is an open source EchoLink proxy for Linux and Windows. It aims to be
efficient and maintain a small footprint, while still implementing all of the
features present in the official EchoLink proxy.

OpenELP also has the ability to bind to multiple network interfaces which are
routed to unique external IP addresses, and therefore is capable of accepting
connections from multiple clients simultaneously.

[![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/cottsay/openelp/ci.yaml?branch=main&event=push&logo=github)](https://github.com/cottsay/openelp/actions/workflows/ci.yaml?query=branch%3Amain+event%3Apush) [![AUR version](https://img.shields.io/aur/version/openelp?logo=arch%20linux&&logoColor=ffffff)](https://aur.archlinux.org/packages/openelp/) [![EPEL package](https://img.shields.io/fedora/v/openelp/epel8?label=epel&logo=red%20hat)](https://src.fedoraproject.org/rpms/openelp) [![Fedora package](https://img.shields.io/fedora/v/openelp?logo=fedora)](https://src.fedoraproject.org/rpms/openelp) [![Codecov](https://img.shields.io/codecov/c/github/cottsay/openelp/main?logo=codecov&logoColor=ffffff)](https://app.codecov.io/gh/cottsay/openelp/branch/main)

Prerequisites
-------------
To build OpenELP you will need:
* [CMake](https://cmake.org/)
* [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/)
* [GCC](https://gcc.gnu.org/) or [Visual Studio](http://aka.ms/vs2015)

If available, OpenELP can use:
* [PCRE2](http://www.pcre.org/)
* [OpenSSL](https://www.openssl.org/)
* [Doxygen](http://www.doxygen.org/)

If your system doesn't have PCRE2 development files installed, you have the
option of bundling PCRE2 with OpenELP. To do this, specify
`-DOPENELP_BUNDLE_PCRE:BOOL=ON` when you call `cmake`. CMake will download
the PCRE2 sources automatically and build them into the OpenELP library.

To create a Windows installer, you will also need to install
[NSIS](http://nsis.sourceforge.net/)

The only runtime dependency that OpenELP has is on the PCRE2 shared library,
unless PCRE2 was bundled into OpenELP.

To install these prerequisites on Fedora and RHEL (w/EPEL), run:
```
sudo dnf install cmake doxygen gcc pcre2-devel pkgconfig openssl-devel
```

Compiling
---------
Linux:

    mkdir build && cd build
    cmake ..
    make

Windows:

    mkdir build && cd build
    cmake .. -DOPENELP_BUNDLE_PCRE:BOOL=ON
    devenv openelp.sln /build

Windows Installer:

    devenv openelp.sln /project PACKAGE /build

Resources
---------
* [APT repository quickstart](https://criede.github.io/openelp/#quickstart) --
  Install OpenELP on Debian and Raspberry Pi OS (daily builds and upstream
  releases)
* [Packaging and upstream sync](.github/UPSTREAM-SYNC.MD) -- How the packages
  and the APT repository of this fork are built and published
* [Upstream project](https://github.com/cottsay/openelp) -- The original
  OpenELP project on GitHub
* [svxlink-db9cr](https://github.com/criede/svxlink-db9cr) -- Companion
  fork that packages SvxLink for Debian and Raspberry Pi OS the same way
  (APT repository with daily builds and upstream releases)
* [Issue tracker](https://github.com/cottsay/openelp/issues) -- Report bugs
  and feature requests

License
-------
See [LICENSE](./LICENSE) file.

EchoLink&reg; is a registered trademark of Synergenics, LLC.

Bugs
----
All issues and feature requests should be directed to
[the bug tracker](https://github.com/cottsay/openelp/issues). Please review any
open issues before filing new ones.

