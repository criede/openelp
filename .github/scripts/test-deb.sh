#!/usr/bin/env bash
set -euo pipefail

# Installation smoke test for the openelp package, run inside a clean
# container of the target suite/architecture with the built .deb mounted
# read-only at /packages. A separate script because 32-bit ARM packages
# are tested in two base images: the Raspbian image they are built in and
# plain Debian armhf, which they are published for as well.
#
# Required environment variables:
#   EXPECTED_ARCH   Debian architecture the package must carry, e.g. "armhf".

: "${EXPECTED_ARCH:?}"
export DEBIAN_FRONTEND=noninteractive

# Fails if any shared library of the given ELF file cannot be resolved.
# An explicit if: bash ignores errexit for a pipeline negated with "!".
assert_libs_resolved() {
  local out
  out="$(ldd "$1")"
  if grep -q 'not found' <<< "$out"; then
    printf '%s\n' "$out" >&2
    echo "Unresolved shared libraries in $1" >&2
    exit 1
  fi
}

apt-get update
apt-get install -y --no-install-recommends /packages/*.deb

test "$(dpkg-query -W -f='${Architecture}' openelp)" = "$EXPECTED_ARCH"
openelpd --version
assert_libs_resolved /usr/bin/openelpd
test -f /usr/lib/systemd/system/openelp.service
test -f /etc/openelp/ELProxy.conf
grep -qx '/etc/openelp/ELProxy.conf' /var/lib/dpkg/info/openelp.conffiles

# Unprivileged service user; configuration readable by its group only.
getent passwd openelp
test "$(stat -c '%U:%G %a' /etc/openelp/ELProxy.conf)" = 'root:openelp 640'
dpkg-statoverride --list /etc/openelp/ELProxy.conf

# Hardening drop-in, and the unit as systemd sees it with the drop-in.
dropin=/usr/lib/systemd/system/openelp.service.d/hardening.conf
grep -qx 'User=openelp' "$dropin"
grep -qx 'NoNewPrivileges=yes' "$dropin"
apt-get install -y --no-install-recommends systemd
systemd-analyze verify /usr/lib/systemd/system/openelp.service

# The packaged configuration must be rejected until a password is set
# (upstream refuses the "notset" placeholder).
if timeout 10 setpriv --reuid=openelp --regid=openelp --clear-groups \
    openelpd -F /etc/openelp/ELProxy.conf; then
  echo "openelpd accepted the unconfigured sample configuration" >&2
  exit 1
fi

# With a password set, the daemon must start as the service user, read the
# real configuration file and keep running until it is stopped (exit
# status 124 = killed by timeout, i.e. it was still running).
sed -i 's/^Password=notset$/Password=smoketest/' /etc/openelp/ELProxy.conf
set +e
timeout 5 setpriv --reuid=openelp --regid=openelp --clear-groups \
  openelpd -F /etc/openelp/ELProxy.conf
rc=$?
set -e
if [[ "$rc" -ne 124 ]]; then
  echo "openelpd (as user openelp) exited early with status ${rc}" >&2
  exit 1
fi

# Removal keeps the user and the (modified) configuration; purge removes
# the statoverride.
apt-get remove -y openelp
test ! -e /usr/bin/openelpd
apt-get purge -y openelp
if dpkg-statoverride --list /etc/openelp/ELProxy.conf; then
  echo "statoverride left behind after purge" >&2
  exit 1
fi
