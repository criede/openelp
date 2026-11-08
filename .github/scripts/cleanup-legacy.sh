#!/usr/bin/env bash
set -euo pipefail

# One-shot maintenance: removes packages that still use the old
# "~<codename>" version suffix from the APT repository on gh-pages and from
# the GitHub releases, and rebuilds and re-signs the repository indices so
# only the "~deb<N>" packages remain. Run via the "Clean up legacy
# packages" workflow, which provides the signing key.
#
# Required environment variables:
#   GH_TOKEN      Token with contents: write.
#   GH_REPO       owner/name of this repository.
#   GPG_KEY_FPR   Fingerprint of the imported APT signing key.

: "${GH_TOKEN:?}" "${GH_REPO:?}" "${GPG_KEY_FPR:?}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
summary="${GITHUB_STEP_SUMMARY:-/dev/stdout}"

# Generates a browsable index.html in every subdirectory (same output as
# publish-apt-repo.sh), so the deleted files no longer appear in listings.
generate_directory_indexes() {
	local repo_dir="$1"
	local dir rel entry name size

	find "$repo_dir" -mindepth 1 -type d | while IFS= read -r dir; do
		rel="/${dir#"$repo_dir"/}"
		{
			printf '<!DOCTYPE html>\n<html lang="en">\n<head><meta charset="utf-8">\n'
			printf '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
			printf '<title>Index of %s</title>\n' "$rel"
			printf '<style>body{font-family:ui-monospace,monospace;max-width:60rem;margin:2rem auto;padding:0 1rem;}a{display:block;padding:0.15rem 0;text-decoration:none;}a:hover{text-decoration:underline;}.size{color:#888;font-size:0.9em;}</style>\n'
			printf '</head>\n<body>\n<h1>Index of %s</h1>\n<a href="../">.. (up)</a>\n' "$rel"
			for entry in "$dir"/*/; do
				[[ -e "$entry" ]] || continue
				name="$(basename "$entry")"
				printf '<a href="%s/">%s/</a>\n' "$name" "$name"
			done
			for entry in "$dir"/*; do
				[[ -f "$entry" ]] || continue
				name="$(basename "$entry")"
				[[ "$name" == "index.html" ]] && continue
				size=$(stat -c%s "$entry" 2>/dev/null || echo '?')
				printf '<a href="%s">%s <span class="size">(%s bytes)</span></a>\n' "$name" "$name" "$size"
			done
			printf '</body>\n</html>\n'
		} > "$dir/index.html"
	done
}

# Rebuilds Packages/Release for every suite/component/architecture present
# in the checkout and signs the Release files, then refreshes the browsable
# indices and the start page.
reindex() {
	local repo_dir="$1"
	local suite_dir suite comp_dir component bin_dir arch
	local components architectures

	(
		cd "$repo_dir"
		for suite_dir in dists/*/; do
			[[ -d "$suite_dir" ]] || continue
			suite="$(basename "$suite_dir")"

			for comp_dir in "$suite_dir"*/; do
				[[ -d "$comp_dir" ]] || continue
				component="$(basename "$comp_dir")"

				for bin_dir in "$comp_dir"binary-*/; do
					[[ -d "$bin_dir" ]] || continue
					arch="$(basename "$bin_dir")"
					arch="${arch#binary-}"
					dpkg-scanpackages --arch "$arch" \
						"pool/$suite/$component" \
						> "${bin_dir}Packages" 2>/dev/null || true
					gzip -9 -k -f "${bin_dir}Packages"
				done
			done

			components=$(cd "$suite_dir" && find . -mindepth 1 -maxdepth 1 \
				-type d -exec basename {} \; | sort -u | tr '\n' ' ')
			architectures=$(cd "$suite_dir" && find . -mindepth 2 -maxdepth 2 \
				-type d -name 'binary-*' -exec basename {} \; \
				| sed 's/^binary-//' | sort -u | tr '\n' ' ')

			apt-ftparchive \
				-o APT::FTPArchive::Release::Origin="OpenELP DB9CR" \
				-o APT::FTPArchive::Release::Label="OpenELP DB9CR" \
				-o APT::FTPArchive::Release::Suite="$suite" \
				-o APT::FTPArchive::Release::Codename="$suite" \
				-o APT::FTPArchive::Release::Components="${components% }" \
				-o APT::FTPArchive::Release::Architectures="${architectures% }" \
				-o APT::FTPArchive::Release::Description="OpenELP DB9CR packages ($suite)" \
				release "dists/$suite" > "dists/$suite/Release"

			gpg --batch --yes --local-user "$GPG_KEY_FPR" --digest-algo SHA512 \
				--detach-sign --armor -o "dists/$suite/Release.gpg" \
				"dists/$suite/Release"
			gpg --batch --yes --local-user "$GPG_KEY_FPR" --digest-algo SHA512 \
				--clearsign -o "dists/$suite/InRelease" "dists/$suite/Release"
		done

		gpg --export --armor "$GPG_KEY_FPR" > pubkey.gpg
		touch .nojekyll
	)

	generate_directory_indexes "$repo_dir"
	REPO_DIR="$repo_dir" bash "$script_dir/render-start-page.sh"
}

# Any .deb whose version ends in a distribution codename rather than
# "~deb<N>" is from before the naming change.
legacy_glob=( -name '*~bookworm_*.deb' -o -name '*~trixie_*.deb' )

repo_url="https://x-access-token:${GH_TOKEN}@github.com/${GH_REPO}.git"
for attempt in 1 2 3 4 5; do
	checkout="$(mktemp -d)"
	git clone --quiet --branch gh-pages --single-branch --depth 1 \
		"$repo_url" "$checkout"
	git -C "$checkout" config user.name 'github-actions[bot]'
	git -C "$checkout" config user.email \
		'41898282+github-actions[bot]@users.noreply.github.com'

	removed=0
	while IFS= read -r f; do
		[[ -n "$f" ]] || continue
		rm -f "$f"
		removed=$((removed + 1))
		echo "Removing ${f#"$checkout"/}"
	done < <(find "$checkout/pool" -type f \( "${legacy_glob[@]}" \) 2>/dev/null)

	reindex "$checkout"

	git -C "$checkout" add -A
	if git -C "$checkout" diff --cached --quiet; then
		echo 'No legacy packages found in the APT repository.' >> "$summary"
		break
	fi

	git -C "$checkout" commit --quiet \
		-m "Remove ${removed} legacy package file(s) and reindex"
	if git -C "$checkout" push --quiet origin HEAD:refs/heads/gh-pages; then
		echo "Removed ${removed} legacy package file(s) from the APT repository." \
			>> "$summary"
		break
	fi

	echo "Push to gh-pages rejected (attempt ${attempt}); retrying." >&2
	rm -rf "$checkout"
	if [[ "$attempt" -eq 5 ]]; then
		echo 'Giving up: could not push to gh-pages.' >&2
		exit 1
	fi
	sleep $((attempt * 15))
done

# Delete the GitHub prereleases whose tag still carries a codename suffix.
mapfile -t daily_legacy < <(
	gh release list --repo "$GH_REPO" --limit 1000 --json tagName \
		--jq '.[].tagName' \
	| grep -E '^daily-.*-(bookworm|trixie)$' || true
)
for tag in "${daily_legacy[@]}"; do
	echo "Deleting legacy daily release: $tag"
	gh release delete "$tag" --repo "$GH_REPO" --yes --cleanup-tag
done

# Delete the codename-suffixed .deb assets from the kept release-* releases.
mapfile -t rel_tags < <(
	gh release list --repo "$GH_REPO" --limit 1000 --json tagName \
		--jq '.[].tagName' \
	| grep -E '^release-' || true
)
for tag in "${rel_tags[@]}"; do
	mapfile -t assets < <(
		gh release view "$tag" --repo "$GH_REPO" --json assets \
			--jq '.assets[].name' \
		| grep -E '[.~](bookworm|trixie)_.*\.deb$' || true
	)
	for asset in "${assets[@]}"; do
		echo "Deleting legacy asset from ${tag}: $asset"
		gh release delete-asset "$tag" "$asset" --repo "$GH_REPO" --yes
	done
done

echo 'Pruned legacy GitHub release artifacts.' >> "$summary"
