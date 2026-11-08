#!/usr/bin/env bash
set -euo pipefail

# Builds the openelp .deb package inside a disposable Debian/Raspbian
# container of the target architecture (native, or QEMU-emulated for 32-bit
# ARM), without a GitHub token. Architecture, codename and base distribution
# are taken from the container itself, so the same script builds for other
# Debian/Raspberry Pi OS/Ubuntu versions and architectures simply by running
# it in a different --platform/base image.
#
# Two kinds of build, selected by BUILD_KIND:
#   daily    (default) Development build of this repository's source;
#            version <x.y.z>[~<extra>]+daily<date>.<run>.<attempt>.g<sha>-1~deb<N>.
#   release  Build of an unmodified upstream release tag (UPSTREAM_TAG, e.g.
#            "v0.9.3"); version <x.y.z>-<revision>~deb<N>, with the
#            Debian revision from release-debian-revision.
# <N> is the Debian major version of the build container (12 for
# bookworm, 13 for trixie; Raspbian uses the same numbers). A numeric
# suffix, as Debian's own backports use, keeps the version order in line
# with distribution upgrades; codenames do not sort chronologically
# (~forky < ~trixie).
#
# Mounts: /source (git checkout to build, read-only), /scripts (this
# directory), /output (receives the package and its metadata).
#
# The package is assembled by hand (cmake --install into a staging root,
# then dpkg-deb --build) instead of via CPack: upstream's CPack setup is
# geared towards the Windows installer, and assembling the package here
# works identically for unmodified upstream release tags.

BUILD_KIND="${BUILD_KIND:-daily}"
case "$BUILD_KIND" in
  daily) ;;
  release)
    : "${UPSTREAM_TAG:?UPSTREAM_TAG is required for BUILD_KIND=release}"
    [[ "$UPSTREAM_TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]
    ;;
  *) echo "Unknown BUILD_KIND: $BUILD_KIND" >&2; exit 1 ;;
esac
export DEBIAN_FRONTEND=noninteractive

arch=$(dpkg --print-architecture)
# Parsed as plain text rather than sourced, so /etc/os-release is never
# executed as shell code.
codename=$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release | tr -d '"')
[[ "$codename" =~ ^[a-z0-9]+$ ]]
# "debian" or "raspbian" (armhf is built against Raspbian, ARMv6+VFP, so it
# also runs on the Raspberry Pi 1/Zero/Zero W; it installs on Debian armhf
# too).
os_id=$(sed -n 's/^ID=//p' /etc/os-release | tr -d '"')
[[ "$os_id" =~ ^[a-z0-9]+$ ]]
# Debian major version for the version suffix (see the header comment).
debian_major=$(sed -n 's/^VERSION_ID=//p' /etc/os-release | tr -d '"' | cut -d. -f1)
[[ "$debian_major" =~ ^[1-9][0-9]*$ ]]
if [[ -n "${EXPECTED_DEBIAN_MAJOR:-}" && "$debian_major" != "$EXPECTED_DEBIAN_MAJOR" ]]; then
  echo "Container is Debian ${debian_major}, expected ${EXPECTED_DEBIAN_MAJOR}." >&2
  exit 1
fi
version_suffix="deb${debian_major}"

apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates git cmake build-essential pkg-config dpkg-dev file \
  libpcre2-dev

multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH)

git config --global --add safe.directory /source
source_sha=$(git -C /source rev-parse HEAD)

# The version lives in the top-level CMakeLists.txt:
#   set(OPENELP_MAJOR_VERSION 0) / MINOR / PATCH
#   set(OPENELP_EXTRA_VERSION "dev" CACHE STRING ...)   ("" on release commits)
cmake_var() {
  sed -n "s/^set(OPENELP_$1_VERSION \\([0-9][0-9]*\\))\$/\\1/p" /source/CMakeLists.txt \
    | tr -d '\r' | head -n 1
}
major=$(cmake_var MAJOR)
minor=$(cmake_var MINOR)
patch=$(cmake_var PATCH)
base_version="${major}.${minor}.${patch}"
if [[ ! "$base_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Could not determine the project version from CMakeLists.txt." >&2
  exit 1
fi
extra_version=$(sed -n 's/^set(OPENELP_EXTRA_VERSION "\([^"]*\)".*/\1/p' /source/CMakeLists.txt \
  | tr -d '\r' | head -n 1)
[[ -z "$extra_version" || "$extra_version" =~ ^[A-Za-z0-9]+$ ]]

date_stamp="$(date -u +%Y%m%d)"
cmake_extra_args=()
if [[ "$BUILD_KIND" == release ]]; then
  # The tag must name exactly the version the tree declares; anything else
  # means the wrong tree was checked out.
  if [[ "v${base_version}" != "$UPSTREAM_TAG" || -n "$extra_version" ]]; then
    echo "CMakeLists.txt declares ${base_version}${extra_version:+-$extra_version}, expected ${UPSTREAM_TAG#v}." >&2
    exit 1
  fi
  # One upstream release maps to exactly one package version per suite,
  # built once and never rebuilt (see plan-release-builds.sh).
  # The Debian revision is raised whenever the packaging changes, so the
  # release packages get rebuilt with the new packaging under a new, higher
  # version (the older ones stay in the pool unchanged).
  revision=$(tr -d ' \r\n' < /scripts/release-debian-revision)
  [[ "$revision" =~ ^[1-9][0-9]*$ ]]
  version="${base_version}-${revision}~${version_suffix}"
else
  # A pre-release suffix ("dev" between releases) is turned into "~dev",
  # so daily builds of 0.9.4-dev sort above the 0.9.3 release and below
  # the 0.9.4 release; on a release commit (no suffix) they sort above it.
  version="${base_version}${extra_version:+~${extra_version}}+daily${date_stamp}.${GITHUB_RUN_NUMBER}.${GITHUB_RUN_ATTEMPT}.g${source_sha:0:12}-1~${version_suffix}"
  # Shown by "openelpd --version" and sent as the registration User-Agent.
  # Only letters, digits and dots: the value is passed as an unquoted
  # preprocessor token and stringified.
  cmake_extra_args+=("-DOPENELP_EXTRA_VERSION=${extra_version:+${extra_version}.}daily${date_stamp}.g${source_sha:0:12}")
fi
dpkg --validate-version "$version"

# Absolute SYSCONF_INSTALL_DIR: openelpd then looks for
# /etc/openelp/ELProxy.conf by default, and the systemd unit uses that path.
# LIB_INSTALL_DIR and SYSTEMD_SERVICES_INSTALL_DIR are PATH-typed cache
# variables upstream: CMake turns an untyped relative -D value for them
# into an absolute path relative to the current directory ("/" here, giving
# /lib/...), so they are passed as absolute paths.
cmake -S /source -B /build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DSYSCONF_INSTALL_DIR=/etc/openelp \
  -DLIB_INSTALL_DIR="/usr/lib/${multiarch}" \
  -DSYSTEMD_SERVICES_INSTALL_DIR=/usr/lib/systemd/system \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=ON \
  -DOPENELP_BUNDLE_PCRE=OFF \
  -DOPENELP_USE_OPENSSL=OFF \
  -DOPENELP_USE_SYSLOG=ON \
  "${cmake_extra_args[@]}"
cmake --build /build --parallel "$(nproc)"
(cd /build && ctest --output-on-failure)

pkgroot=/pkgroot
rm -rf "$pkgroot"
DESTDIR="$pkgroot" cmake --install /build
# Everything must land under /usr or /etc.
unexpected=$(find "$pkgroot" -mindepth 1 -maxdepth 1 ! -name usr ! -name etc)
if [[ -n "$unexpected" ]]; then
  echo "Unexpected top-level install paths: ${unexpected}" >&2
  exit 1
fi

# Runtime package only: the headers and the unversioned .so development
# symlink are not shipped (libopenelp is used by openelpd only).
rm -rf "$pkgroot/usr/include"
find "$pkgroot/usr/lib" -name 'libopenelp.so' -type l -delete

test -x "$pkgroot/usr/bin/openelpd"
test -f "$pkgroot/etc/openelp/ELProxy.conf"
test -f "$pkgroot/usr/lib/systemd/system/openelp.service"
grep -qF 'ExecStart=/usr/bin/openelpd ' "$pkgroot/usr/lib/systemd/system/openelp.service"
grep -qF '/etc/openelp/ELProxy.conf' "$pkgroot/usr/lib/systemd/system/openelp.service"
mapfile -t libs < <(find "$pkgroot/usr/lib/${multiarch}" -name 'libopenelp.so.*')
test "${#libs[@]}" -gt 0

# The configuration file holds the proxy password. Shipped root-only;
# postinst makes it readable for the service group via dpkg-statoverride.
chmod 0600 "$pkgroot/etc/openelp/ELProxy.conf"

# Run the proxy unprivileged and sandboxed. A drop-in rather than an edit
# of upstream's unit, so the unit stays as upstream ships it. No
# PrivateDevices=: it would hide /dev/log, which openelpd -S logs to.
# AF_NETLINK is needed by getaddrinfo() (registration). The bind
# capability keeps configured ports below 1024 working as they did as root.
install -d -m 0755 "$pkgroot/usr/lib/systemd/system/openelp.service.d"
cat > "$pkgroot/usr/lib/systemd/system/openelp.service.d/hardening.conf" <<'EOF'
# Shipped by the openelp package: run the proxy as an unprivileged user in
# a systemd sandbox. Override in /etc/systemd/system/openelp.service.d/.
[Service]
User=openelp
Group=openelp
UMask=0077
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
AmbientCapabilities=CAP_NET_BIND_SERVICE
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
DevicePolicy=closed
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
ProtectProc=invisible
ProcSubset=pid
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX AF_NETLINK
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RemoveIPC=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
EOF
if [[ -f "$pkgroot/usr/share/man/man1/openelpd.1" ]]; then
  gzip -9n "$pkgroot/usr/share/man/man1/openelpd.1"
fi
install -D -m 0644 /source/LICENSE "$pkgroot/usr/share/doc/openelp/copyright"
find "$pkgroot" -mindepth 1 -exec chmod go-w {} +

# Runtime dependencies (libpcre2-8, libc) via dpkg-shlibdeps rather than
# hand-listed, since package names differ between suites. -l lets it find
# the package's own libopenelp; there is no shlibs file for it, so
# --ignore-missing-info then skips it (it is in this very package).
mkdir -p /tmp/shlibdeps/debian
cat > /tmp/shlibdeps/debian/control <<'EOF'
Source: openelp
Section: hamradio
Priority: optional
Maintainer: OpenELP DB9CR packaging <criede@users.noreply.github.com>

Package: openelp
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: Open Source EchoLink Proxy
EOF
(
  cd /tmp/shlibdeps
  dpkg-shlibdeps --ignore-missing-info -O \
    "-l${pkgroot}/usr/lib/${multiarch}" \
    "$pkgroot/usr/bin/openelpd" "${libs[@]}" > shlibdeps.out
)
shlibs_depends=$(sed -n 's/^shlibs:Depends=//p' /tmp/shlibdeps/shlibdeps.out)
test -n "$shlibs_depends"

installed_size=$(du -sk --exclude=DEBIAN "$pkgroot" | cut -f1)
mkdir -p "$pkgroot/DEBIAN"
cat > "$pkgroot/DEBIAN/control" <<EOF
Package: openelp
Version: ${version}
Architecture: ${arch}
Maintainer: OpenELP DB9CR packaging <criede@users.noreply.github.com>
Installed-Size: ${installed_size}
Depends: ${shlibs_depends}, passwd
Section: hamradio
Priority: optional
Homepage: https://github.com/cottsay/openelp
Description: Open Source EchoLink Proxy
 OpenELP is an open source EchoLink proxy. It forwards the EchoLink UDP
 traffic of a client (an EchoLink node such as SvxLink, or the EchoLink
 PC client) over a single TCP connection, for clients that cannot accept
 inbound UDP on ports 5198/5199 themselves.
 .
 This package contains the openelpd daemon, its library, a sample
 configuration in /etc/openelp/ELProxy.conf and a systemd service
 (openelp.service, not enabled automatically), which runs the proxy as
 the unprivileged user "openelp" in a systemd sandbox.
EOF
( cd "$pkgroot" && find etc -type f -printf '/%p\n' | sort ) > "$pkgroot/DEBIAN/conffiles"

cat > "$pkgroot/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
  ldconfig
  # Unprivileged service user (see openelp.service.d/hardening.conf).
  if ! getent passwd openelp >/dev/null; then
    useradd --system --user-group --no-create-home --home-dir /nonexistent \
      --shell /usr/sbin/nologin --comment "OpenELP EchoLink proxy" openelp
  fi
  # The configuration holds the proxy password: readable by root and the
  # service group only. A statoverride survives package upgrades.
  if ! dpkg-statoverride --list /etc/openelp/ELProxy.conf >/dev/null; then
    dpkg-statoverride --update --add root openelp 0640 /etc/openelp/ELProxy.conf
  fi
  if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    # Upgrades restart a running proxy so the new version takes effect.
    if [ -n "$2" ]; then
      systemctl try-restart openelp.service || true
    fi
  fi
fi
EOF
cat > "$pkgroot/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] && [ -d /run/systemd/system ]; then
  systemctl stop openelp.service || true
fi
EOF
cat > "$pkgroot/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
  ldconfig
  if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
  fi
fi
if [ "$1" = purge ]; then
  if dpkg-statoverride --list /etc/openelp/ELProxy.conf >/dev/null; then
    dpkg-statoverride --remove /etc/openelp/ELProxy.conf
  fi
  if [ -x /usr/bin/deb-systemd-helper ]; then
    deb-systemd-helper purge openelp.service >/dev/null || true
  fi
fi
EOF
chmod 0755 "$pkgroot/DEBIAN/postinst" "$pkgroot/DEBIAN/prerm" "$pkgroot/DEBIAN/postrm"

deb="/output/openelp_${version}_${arch}.deb"
dpkg-deb --build --root-owner-group "$pkgroot" "$deb"

test "$(dpkg-deb -f "$deb" Architecture)" = "$arch"
test "$(dpkg-deb -f "$deb" Version)" = "$version"
dpkg-deb --info "$deb"
# Captured first rather than piped into grep -q (SIGPIPE under pipefail).
package_contents="$(dpkg-deb -c "$deb")"
printf '%s\n' "$package_contents"
grep -q '\./usr/bin/openelpd$' <<< "$package_contents"
grep -q '/libopenelp\.so\.' <<< "$package_contents"

printf '%s\n' "$version" > /output/VERSION
printf '%s\n' "$codename" > /output/SUITE
printf '%s\n' "$arch" > /output/ARCH
printf '%s\n' "$BUILD_KIND" > /output/KIND
printf '%s\n' "$source_sha" > /output/SOURCE_SHA
# Empty for daily builds; always written so the hand-over has a fixed shape.
printf '%s\n' "${UPSTREAM_TAG:-}" > /output/UPSTREAM_TAG

if [[ "$os_id" == raspbian ]]; then
  build_env_note="- Built in a clean Raspbian ${codename} (ARMv6+VFP) container, so it runs on
  every Raspberry Pi model with 32-bit Raspberry Pi OS (including Pi 1/Zero/
  Zero W) and also installs on Debian ${codename} armhf; installation is
  smoke-tested in clean Raspbian and Debian containers."
else
  build_env_note="- Built in a clean Debian ${codename} container; installation is
  smoke-tested in a clean container."
fi
if [[ "$BUILD_KIND" == release ]]; then
  build_headline="Package build of the unmodified upstream OpenELP release ${UPSTREAM_TAG}
(cottsay/openelp tag \`${UPSTREAM_TAG}\`). None of the criede/openelp source
changes are included, in particular none of its security mitigations for
SvxLink (use the \`main\` component for those); the systemd hardening is
part of the packaging and included."
  apt_component=release
else
  build_headline="Daily development build of criede/openelp, not a stable release.

Includes this fork's security changes: dropping packets that would trigger
SvxLink vulnerabilities (malformed NDATA packets, CVE-2026-73152 /
GHSA-2m63-p9h9-p5rf; RTCP SDES packets with an unsafe callsign,
GHSA-6w9x-486p-ggrj) and an authorization timeout for proxy clients."
  apt_component=main
fi
cat > /output/BUILD-INFO.MD <<EOF
${build_headline}

- Target: Debian/Raspberry Pi OS ${codename} (${arch}).
- Package version: ${version}
- Source commit: ${source_sha}
- Build: ${GITHUB_RUN_NUMBER}, attempt ${GITHUB_RUN_ATTEMPT}
- Contents: openelpd, libopenelp, /etc/openelp/ELProxy.conf (conffile,
  root:openelp 0640),
  openelp.service (systemd, not enabled automatically; runs as the
  unprivileged user openelp in a systemd sandbox), man page.
- Built against the system PCRE2 (libpcre2-8); MD5 from the bundled
  implementation (no OpenSSL dependency); syslog logging enabled.
- The upstream unit tests (ctest) passed in the build container.
${build_env_note}
- Operation with real EchoLink clients has not been tested by CI.

Install with \`sudo apt install ./openelp_*.deb\` after downloading the
package, or via the APT repository (component \`${apt_component}\`):
https://criede.github.io/openelp/
Set a password in /etc/openelp/ELProxy.conf before starting the service.
EOF

cd /output
# Per-suite/architecture name: several legs share one GitHub release.
sha256sum ./*.deb > "SHA256SUMS-${codename}-${arch}"
