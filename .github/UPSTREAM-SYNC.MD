# Upstream sync, Debian packages and APT repository

This fork (`criede/openelp`) tracks the upstream project
[`cottsay/openelp`](https://github.com/cottsay/openelp), builds Debian
packages of it daily and for every upstream release, and publishes them in
a GPG-signed APT repository on GitHub Pages:
<https://criede.github.io/openelp/>.

The setup follows the one of
[`criede/svxlink-db9cr`](https://github.com/criede/svxlink-db9cr) and lives
across several workflow files with separate responsibilities:

- `.github/workflows/sync-upstream.yml` merges upstream, then calls
  `build-and-publish.yml`. Specific to this fork/repository.
- `.github/workflows/build-deb.yml` is the reusable building block that
  builds, unit-tests and installation-tests the package for one
  suite/architecture (see `.github/build-targets.json`) and hands it over
  as an Actions artifact. Used by both workflows below.
- `.github/workflows/build-releases.yml` builds packages of the upstream
  release tags (see "Upstream release packages").
- `.github/workflows/build-and-publish.yml` builds the daily packages and
  publishes them (GitHub prerelease, APT repository on GitHub Pages).
  Deliberately repository-agnostic: it takes the commit to build as an
  input, has no repository-name guard, and can be run standalone via
  `workflow_dispatch` as well as being called (`workflow_call`) by
  `sync-upstream.yml`.
- `.github/workflows/refresh-start-page.yml` re-renders the repository
  start page when only its templates change; `check-translations.yml`
  checks that every translation of it is current.
- `.github/scripts/` holds the shell scripts the workflows run;
  `.github/pages/` the start page templates.

A run triggered from `Sync upstream` does **not** show up under
**Actions → Build and publish**'s own run history: a `workflow_call`
executes as jobs nested inside the caller's run, visible under
**Actions → Sync upstream** as jobs prefixed `Build and publish / ...`.

The upstream workflows `ci.yaml` and `release.yaml` are left untouched.
They run on pushes to `main` of this fork as well; disable them under
**Actions** if they are not wanted here.

## Daily upstream sync

`Sync upstream` merges all new commits from `cottsay/openelp`, branch
`main`, into `criede/openelp`, branch `main`, daily at 02:17 UTC. Other
branches, tags and GitHub releases are not synchronized. Conflicting hunks
are resolved in favor of this fork (`-X ours`); structural conflicts
(modify/delete) are resolved from this fork's state.

`.github/workflows/` is fully excluded from the merge: before the merge
commit it is reset to this fork's own state, so upstream can neither add,
change nor delete workflows here (also for an otherwise possible
fast-forward, which `--no-ff` prevents).

After the merge, the `build-and-publish` job calls
`build-and-publish.yml` with the synchronized commit as `source_sha`. On
the daily **schedule** this is skipped if nothing new was merged; a
**manual** run always builds, e.g. to test workflow changes.

The sync never force-pushes. If `main` changes during a run, the push
fails and the workflow can simply be started again.

## Daily packages

The `daily-deb` job runs as a matrix over the targets in
`.github/build-targets.json` (shared with the release builds):

| Suite      | Debian version        | Architecture | Build image | Runner | Covers |
|------------|-----------------------|--------------|-------------|--------|--------|
| `bookworm` | Debian 12 (oldstable) | `arm64` | `debian:bookworm-slim` | `ubuntu-24.04-arm` (native) | Raspberry Pi OS Bookworm 64-bit, Debian 12 arm64 |
| `trixie`   | Debian 13 (stable)    | `arm64` | `debian:trixie-slim`   | `ubuntu-24.04-arm` (native) | Raspberry Pi OS Trixie 64-bit, Debian 13 arm64 |
| `bookworm` | Debian 12 (oldstable) | `amd64` | `debian:bookworm-slim` | `ubuntu-24.04` (native) | Debian 12 on Intel/AMD |
| `trixie`   | Debian 13 (stable)    | `amd64` | `debian:trixie-slim`   | `ubuntu-24.04` (native) | Debian 13 on Intel/AMD |
| `bookworm` | Debian 12 (oldstable) | `i386`  | `debian:bookworm-slim` | `ubuntu-24.04` (native 32-bit userland) | Debian 12 on 32-bit PCs |
| `bookworm` | Debian 12 (oldstable) | `armhf` | Raspbian bookworm (built locally) | `ubuntu-24.04` (QEMU) | Raspberry Pi OS Bookworm 32-bit (all Pi models), Debian 12 armhf |
| `trixie`   | Debian 13 (stable)    | `armhf` | Raspbian trixie (built locally)   | `ubuntu-24.04` (QEMU) | Raspberry Pi OS Trixie 32-bit (all Pi models), Debian 13 armhf |

Debian and Raspberry Pi OS share the same package base per codename, so
one build per suite/architecture covers both. When a Debian release
changes support status, adjust `.github/build-targets.json`.

32-bit specifics:

- **armhf** is built in a Raspbian container rather than Debian's own
  `armhf`: Raspbian targets ARMv6+VFP (every Raspberry Pi, including
  Pi 1/Zero/Zero W), Debian `armhf` ARMv7. The Raspbian build also
  installs on Debian `armhf`, so the installation test runs in both a
  Raspbian and a `debian:<suite>-slim` `linux/arm/v7` container. The
  Raspbian image is created per run with `mmdebstrap` from
  `http://raspbian.raspberrypi.com/raspbian` (no HTTPS; integrity comes
  from the signed `InRelease`) and `docker import`ed. Raspbian's archive
  key is vendored as `.github/keys/raspbian-archive-keyring.asc`
  (fingerprint `A0DA38D0D76E8B5D638872819165938D90FDDD2E`, checked by the
  workflow). These legs run under QEMU user-mode emulation on an amd64
  runner (Ubuntu's `qemu-user-static`, no third-party action), hence the
  longer timeout.
- **i386** is built for bookworm only: Debian 13 no longer supports
  installing i386 systems.

The legs run in parallel with `fail-fast: false`. Each successful leg
uploads its package as a short-lived Actions artifact; a single
`publish-daily-deb` job then publishes every leg that built and passed its
tests, so there is no push race on `gh-pages`. A failed leg still marks
the run as failed but does not withhold the others.

### What a leg does

`.github/scripts/build-deb.sh` runs inside the build container (no GitHub
token):

1. Installs the build dependencies (`cmake`, `build-essential`,
   `pkg-config`, `libpcre2-dev`, `dpkg-dev`, ...).
2. Configures OpenELP with `CMAKE_INSTALL_PREFIX=/usr`,
   `SYSCONF_INSTALL_DIR=/etc/openelp` (so `openelpd` and the systemd unit
   use `/etc/openelp/ELProxy.conf`), the library in the multiarch
   directory, the system PCRE2, the bundled MD5 implementation (no OpenSSL
   dependency) and syslog support.
3. Builds and runs the upstream unit tests (`ctest`).
4. Installs into a staging root and assembles the `.deb` with
   `dpkg-deb --build` (not CPack: upstream's CPack setup targets the
   Windows installer, and assembling the package here works identically
   for unmodified upstream release tags). Headers and the unversioned
   `libopenelp.so` symlink are dropped (runtime package only); runtime
   dependencies come from `dpkg-shlibdeps`. Maintainer scripts run
   `ldconfig`, `systemctl daemon-reload`, restart a running proxy on
   upgrade and stop it on removal. The service is **not** enabled
   automatically: the sample configuration must be edited first.
5. Hardens the service: a drop-in
   `/usr/lib/systemd/system/openelp.service.d/hardening.conf` (upstream's
   unit itself stays unchanged) runs the proxy as the system user
   `openelp`, created by `postinst`, with `NoNewPrivileges`,
   `ProtectSystem=strict`, an empty capability set except
   `CAP_NET_BIND_SERVICE` (so ports below 1024 keep working), restricted
   address families, `MemoryDenyWriteExecute`, a `@system-service`
   system call filter and further sandboxing options. `PrivateDevices` is
   deliberately not used, since it would hide `/dev/log`, which
   `openelpd -S` logs to. The configuration file is a dpkg conffile owned
   `root:openelp` with mode `0640` (it holds the proxy password), set via
   `dpkg-statoverride` so upgrades keep it; purge removes the override.
   Override options in `/etc/systemd/system/openelp.service.d/` if needed.
   This is packaging, so it applies to both components.

`.github/scripts/test-deb.sh` then installs the package in a fresh
container of the same suite and checks the files, the conffile and its
ownership/mode, the service user, the drop-in (`systemd-analyze verify`),
that all shared libraries resolve, that `openelpd` rejects the unconfigured
sample configuration, that it starts and keeps running **as the user
`openelp`** with a password set in the real configuration file, and that
the package removes and purges cleanly.

### Security changes in this fork

The `main` (daily) packages contain this fork's source changes on top of
upstream; the `release` packages do not. They are listed, with what they
mitigate, in the IMPORTANT box of the [README](../README.md):

- `src/packet_filter.c` (unit test `test/test_packet_filter.c`): packets
  from the EchoLink network that would trigger SvxLink vulnerabilities are
  dropped before forwarding — malformed NDATA packets on UDP 5198
  (CVE-2026-73152 / GHSA-2m63-p9h9-p5rf) and RTCP SDES packets with an
  unsafe callsign on UDP 5199 (GHSA-6w9x-486p-ggrj). Drops are logged at
  INFO level, at most once per minute and kind per client, with a count
  of suppressed drops.
- `src/proxy.c`/`src/conn.c`: a 10 s receive timeout during client
  authorization (`conn_set_timeout()` now acts on the connected socket).
- `src/registration.c`: the registration suffix buffer accounts for
  `RegistrationName` (heap overflow with names longer than about 50
  characters).

When SvxLink publishes further fixes for remotely reachable parsers, check
whether OpenELP forwards the triggering packets and, if so, consider a
matching filter; keep the README list and the start page in sync.

### Version scheme

Daily builds: `0.9.4~dev+daily20261108.42.1.g0123456789ab-1~trixie` —
project version from the top-level `CMakeLists.txt`
(`OPENELP_MAJOR/MINOR/PATCH_VERSION`), the pre-release suffix
`OPENELP_EXTRA_VERSION` (`dev` between releases) as `~dev`, UTC date,
workflow run number, attempt, commit short hash and target suite. The
`~dev` makes daily builds of the next version sort above the previous
release and below the next one. `openelpd --version` shows
`0.9.4-dev.daily20261108.g0123456789ab`.

### Publishing

`publish-packages.sh` (shared with the release builds) uploads each leg's
package and `SHA256SUMS-<suite>-<arch>` into the daily prerelease of the
suite (`daily-<version>`, all architectures of a suite share it, never
marked "Latest"), then publishes the packages into the APT repository's
`main` component. The prerelease tag points to the built commit, or to
the default branch if that commit's `.github/workflows/` differ from it
(the `GITHUB_TOKEN` may not create a ref that would count as a workflow
change); the release notes always name the exact source commit.

## Upstream release packages

`build-releases.yml` packages the official upstream releases: every
published (non-draft, non-prerelease) `cottsay/openelp` release with a tag
`v<x>.<y>.<z>` of version 0.9.3 or newer (`MIN_VERSION` in
`plan-release-builds.sh`), for every target in `build-targets.json`.

- **Source:** the unmodified upstream release tag. None of this fork's
  source changes are applied; only the build settings are the same as for
  the daily packages. The build checks that the tag matches the version
  declared in `CMakeLists.txt`.
- **Version:** `<x.y.z>-<revision>~<suite>`, e.g. `0.9.3-2~trixie`. The
  Debian revision comes from `.github/scripts/release-debian-revision`.
  Raise it whenever the packaging changes (build options, files, maintainer
  scripts, the systemd drop-in): the planning step then finds every release
  missing in the new revision and builds it again, so all kept releases get
  the new packaging under a new, higher version, and `apt upgrade` installs
  it. Packages of older revisions stay in the pool unchanged, so a given
  version always means the same bits. Revision 1 was built before the
  systemd hardening, revision 2 includes it.
- **Publishing:** one GitHub release `release-<tag>` per upstream release
  (the highest is marked "Latest") holding the packages of all
  suites/architectures, and the APT repository's `release` component.
  Neither is ever pruned. The tag points to this repository's default
  branch (upstream tag commits carry upstream's own workflows, see above);
  the release notes name the upstream tag and commit.
- **When:** daily at 05:47 UTC and on manual dispatch. The `plan` job
  compares the upstream releases with the `release` pool on `gh-pages` and
  builds only what is missing: the existing releases once, then each new
  upstream release, plus any target added to `build-targets.json` later.
  A release package is never rebuilt once it is in the pool.
- **Failures:** a combination whose build failed or timed out within the
  last 7 days (from this workflow's run history, job name
  `<tag> <suite>/<arch>`) is not retried automatically; dispatch the
  workflow with `retry_failed` to retry earlier.

Both package workflows push to `gh-pages`; `publish-packages.sh` re-clones
and re-applies its packages if a push is rejected because the other
pushed in between, and all Pages deploy jobs share one concurrency group.

## APT repository on GitHub Pages

The repository is committed to the `gh-pages` branch, which serves only as
the persistent data store. A separate `deploy-pages` job publishes its
content via `actions/configure-pages`, `actions/upload-pages-artifact` and
`actions/deploy-pages` — the **GitHub Actions** Pages source, not the
classic **Deploy from a branch** source (which would make GitHub run its
own `pages-build-deployment` workflow outside this repository's control).

Layout (standard Debian repository layout, extensible with further
suites/architectures without touching existing paths):

```
dists/<suite>/InRelease
dists/<suite>/Release
dists/<suite>/Release.gpg
dists/<suite>/<component>/binary-<arch>/Packages[.gz]
pool/<suite>/<component>/<letter>/<package>/<package>_<version>_<arch>.deb
pubkey.gpg                                   # public signing key
index.html, index.<lang>.html                # start page and translations
```

`<component>` is `main` (daily builds) or `release` (upstream releases).
`publish-apt-repo.sh` places each package in the pool, prunes old daily
packages (see "Retention"), rebuilds `Packages`/`Packages.gz` with
`dpkg-scanpackages`, rebuilds and signs `Release`/`InRelease` with
`apt-ftparchive` and `gpg`, exports `pubkey.gpg`, generates browsable
directory indexes and renders the start page.

### Start page and translations

The start page is rendered by `render-start-page.sh` from the English
master template `.github/pages/index.html.in`, filling in only what
depends on the repository state (supported-systems table per component, a
placeholder row for any package not described in the template, language
links). It is also published in the European languages listed in
`.github/pages/languages.tsv` — the 24 official EU languages plus the
national languages of the other European states — as `index.<lang>.html`,
from the translated templates in `.github/pages/i18n/`. Every translated
page shows a notice in its own language that it was translated by AI, with
a link to the English original; the generator inserts that notice (text
from `languages.tsv`), so it cannot be left out. Every translation
addresses the reader the way radio amateurs address each other:
informally, in the second person singular (German "du", French "tu",
Spanish "tú", Russian "ты", ...), never with a formal or plural form.

Whenever the English template changes, **every** translation must be
updated in the same change: translate the changed text, keep the HTML
structure and the `@@...@@` placeholders identical, and set each
translation's `<meta name="source-sha256" content="...">` to the hash
printed by `bash .github/scripts/check-translations.sh` (which must then
report every language as `current`). The `Check start page translations`
workflow runs it with `--strict` on every change under `.github/pages/`.
Publishing still renders an outdated translation (with a workflow
warning) rather than blocking package publication. `.github/.gitattributes`
keeps LF line endings, so the hash is the same in every checkout.

The package table in the template must list what is actually built:
whenever `build-deb.sh` changes the package contents or build options,
update the template and all translations in the same change.

### Signing key

`Release` is signed with a GPG key (`Release.gpg` detached, `InRelease`
inline). The private key exists only as the repository secret
**APT_SIGNING_KEY** (ASCII-armored), is imported during the publish job and
removed afterwards. It has no passphrase so signing works unattended; its
only protection is access control on the secret.

Generate a key locally, for example:

```sh
gpg --batch --gen-key <<'EOF'
%no-protection
Key-Type: RSA
Key-Length: 4096
Key-Usage: sign
Name-Real: OpenELP DB9CR APT Repository
Name-Comment: Package signing key
Expire-Date: 3y
%commit
EOF
gpg --list-secret-keys --keyid-format=long "OpenELP DB9CR APT Repository"
```

Store it as a secret and delete the export:

```sh
gpg --export-secret-keys --armor <FINGERPRINT> > apt-signing-key-private.asc
gh secret set APT_SIGNING_KEY --repo criede/openelp < apt-signing-key-private.asc
rm apt-signing-key-private.asc
```

The public key is exported freshly on every publish to `pubkey.gpg`. After
a key change, users must import the key again.

### Retention

Daily prereleases and the daily packages in the pool follow the same
grandfather-father-son policy (`gfs-retention.sh`), applied per suite for
releases and per package/suite/architecture for the pool:

| Age               | Kept                          |
|-------------------|-------------------------------|
| < 1 week          | every build                   |
| 1 week – 1 month  | the newest build per ISO week |
| 1 month – 1 year  | the newest build per month    |
| ≥ 1 year          | nothing                       |

Only versions carrying a `+daily<YYYYMMDD>` marker are ever pruned;
upstream release packages are kept forever. The pool is pruned by
`publish-apt-repo.sh` on every publish, the prereleases by the
`cleanup-old-releases` job (`prune-daily-releases.sh`), so a version
disappears from both at the same time.

## Installing on Raspberry Pi OS and Debian

| Operating system                             | Codename   |
|----------------------------------------------|------------|
| Raspberry Pi OS Trixie (64-bit and 32-bit)   | `trixie`   |
| Raspberry Pi OS Bookworm (64-bit and 32-bit) | `bookworm` |
| Debian 13 (Trixie)                           | `trixie`   |
| Debian 12 (Bookworm)                         | `bookworm` |

### Choosing a component: `main` or `release`

| Component | Contains | Source | Updates via `apt upgrade` | Kept |
|-----------|----------|--------|---------------------------|------|
| `main`    | Daily development builds | This fork's `main` (upstream `main` plus this fork's changes) | Every new daily build | Retention window |
| `release` | Official upstream OpenELP releases, 0.9.3 and newer | The unmodified upstream release tag | Each new upstream release | Forever |

Use `release` for a proxy that should run a known, released version (the
default in the quickstart commands, here and on the repository's start
page); use `main` to follow current development, including this fork's
own source changes. Configure only one of them: with both enabled, apt
installs whichever version is higher, which is usually a daily build.

```sh
# Import the public signing key
curl -fsSL https://criede.github.io/openelp/pubkey.gpg | \
  sudo gpg --dearmor -o /usr/share/keyrings/openelp-db9cr.gpg

# Add the repository source. Component: "release" (default) for the
# official upstream releases (0.9.3 and newer), "main" for the daily builds.
component=release
codename=$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release | tr -d '"')
arch=$(dpkg --print-architecture)
echo "deb [signed-by=/usr/share/keyrings/openelp-db9cr.gpg arch=${arch}] https://criede.github.io/openelp ${codename} ${component}" | \
  sudo tee /etc/apt/sources.list.d/openelp-db9cr.list

sudo apt update
sudo apt install openelp

# Mandatory before the first start: set a password.
sudo editor /etc/openelp/ELProxy.conf

sudo systemctl enable --now openelp.service
systemctl status openelp.service
journalctl -u openelp.service -f
```

`openelpd` refuses to start while `Password` is still `notset`. OpenELP
reads its configuration only at startup; apply changes with
`sudo systemctl restart openelp.service`. Local changes to
`/etc/openelp/ELProxy.conf` are kept on upgrades (dpkg conffile). The
service runs as the unprivileged user `openelp`, which can read the
configuration via its group.

Network: clients connect over TCP to `Port` (default 8100); the proxy
must be reachable from the Internet on UDP 5198 and 5199 and needs
outgoing TCP 5200 and UDP 5198/5199. For SvxLink, set `PROXY_SERVER`,
`PROXY_PORT` and `PROXY_PASSWORD` in `ModuleEchoLink.conf`.

If the detected codename is not built (yet), `apt update` reports a 404;
set the codename in `/etc/apt/sources.list.d/openelp-db9cr.list` to one of
the values above.

### Switching between `main` and `release`

```sh
sudo sed -i 's/ main$/ release/' /etc/apt/sources.list.d/openelp-db9cr.list
sudo apt update
apt list -a openelp                       # shows the available versions
sudo apt install openelp=0.9.3-2~trixie   # usually a downgrade
```

and back:

```sh
sudo sed -i 's/ release$/ main/' /etc/apt/sources.list.d/openelp-db9cr.list
sudo apt update && sudo apt upgrade
```

### Rolling back to an older build

```sh
apt list -a openelp
sudo apt install openelp=0.9.4~dev+daily20261108.12.1.g0123456789ab-1~trixie
sudo apt-mark hold openelp     # optional; "apt-mark unhold openelp" to resume updates
```

## Setup

1. Push the workflows to the default branch (`main`).
2. Enable GitHub Actions for the repository (on forks, scheduled
   workflows may be disabled initially; enable them under **Actions**).
3. Create the repository secret **APT_SIGNING_KEY** (see "Signing key").
4. GitHub Pages: enable it once with the **GitHub Actions** source, with
   an admin account: `gh api -X POST repos/criede/openelp/pages -f
   build_type=workflow` (or **Settings → Pages → Build and deployment →
   Source → GitHub Actions**). `actions/configure-pages` does not create
   the site by itself (without its `enablement` option, which needs more
   than the `GITHUB_TOKEN` grants); without this step `deploy-pages` fails
   with "Get Pages site failed ... Not Found". If Pages was set up as
   **Deploy from a branch** before, switch it instead with
   `gh api -X PUT repos/criede/openelp/pages -f build_type=workflow`.
5. Start a first run under **Actions → Sync upstream → Run workflow** (or
   **Actions → Build and publish → Run workflow**; a `source_sha` typed
   there must be the full 40-character SHA). Then run
   **Actions → Build upstream releases → Run workflow** to build the
   release backlog right away instead of waiting for the schedule.

If a `github-pages` environment with a custom deployment branch policy
exists, add `main` to its allowed branches (**Settings → Environments →
github-pages**), otherwise `deploy-pages` fails with "Branch ... is not
allowed to deploy to github-pages".

The workflows use the `GITHUB_TOKEN` with `contents: write`; branch rules
must allow its direct pushes to `main` and `gh-pages`. Optionally, a
repository secret **UPSTREAM_SYNC_TOKEN** (fine-grained PAT for this
repository with **Contents** and **Workflows** read/write) is used by the
sync in preference; store token values only as secrets.

GitHub disables scheduled workflows in public repositories after 60 days
without a push. Since the build is skipped whenever upstream has nothing
new, the sync pushes a trivial heartbeat commit (updates
`.github/HEARTBEAT`) whenever `main`'s last commit is 25 or more days old.

## Sources

- [GitHub: Events that trigger workflows](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
- [GitHub: GITHUB_TOKEN](https://docs.github.com/en/actions/concepts/security/github_token)
- [GitHub Pages: Publishing with a custom GitHub Actions workflow](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site#publishing-with-a-custom-github-actions-workflow)
- [GitHub Actions: Reusing workflows](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)
- [Debian Wiki: SecureApt](https://wiki.debian.org/SecureApt)
- [Debian Repository Format](https://wiki.debian.org/DebianRepository/Format)
- [Debian Policy Manual: conffiles](https://www.debian.org/doc/debian-policy/ch-files.html#configuration-files)
