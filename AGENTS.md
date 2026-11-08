# Notes for agents

This repository (`criede/openelp`) is a fork of
[`cottsay/openelp`](https://github.com/cottsay/openelp) that adds Debian
packaging and an APT repository on GitHub Pages. See
`.github/UPSTREAM-SYNC.MD` for how the sync, builds and publishing work.

## Rules for the packaging setup

- Keep `.github/workflows/` under this fork's control: the daily sync
  never takes over upstream workflow changes. Do not edit upstream's
  `ci.yaml`/`release.yaml` for packaging purposes; add separate workflows.
- Use currently maintained Node.js runtimes in all actions (check each
  action's `runs.using`), pin actions by commit SHA, and deploy GitHub
  Pages via the GitHub Actions source, never "Deploy from a branch".
- Before adding a package name to an `apt-get install` line, verify it
  exists for every target suite (packages.debian.org), rather than
  guessing.
- The start page template `.github/pages/index.html.in` must describe
  exactly what `build-deb.sh` builds. Whenever the template changes,
  update **every** translation in `.github/pages/i18n/` in the same
  change: keep HTML structure and `@@...@@` placeholders identical, set
  `<meta name="source-sha256">` to the hash printed by
  `bash .github/scripts/check-translations.sh`, which must then report
  every language as `current`. Do not put the AI-translation notice into
  the templates; the generator inserts it from `languages.tsv`. Every
  translation addresses the reader the way radio amateurs address each
  other: informally, in the second person singular (German "du", French
  "tu", Spanish "tú", Dutch "je", Polish "ty", Russian "ты", ...), never
  with a formal or plural form (German "Sie", French "vous", ...).
- The IMPORTANT box at the top of `README.md` lists every security change
  of this fork and what it mitigates. Whenever a mitigation is added,
  changed or removed (e.g. in `src/packet_filter.c`), update that list,
  the "Security changes in this fork" section of
  `.github/UPSTREAM-SYNC.MD` and the start page (with all translations)
  in the same change.
- Documentation under `.github/` and this file is written in English.
- Upstream sources stay as close to upstream as possible; keep local
  source changes small and self-contained so merges stay easy.
