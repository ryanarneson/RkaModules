#!/usr/bin/env bash
# Bump the build number (plugin.json's REVISION, 2.MINOR.REVISION) before
# every build handed over for testing: `make install`, tools/mac_dist.sh,
# tools/cross_build.sh. Mac and Windows packages built from one commit share
# one version. Adds a line under "Test builds" in CHANGELOG.md and commits
# both, so each version names exactly one commit.
#
#   tools/bump_version.sh "Fog: ICE octave, wet leveller"
#
# Releases bump MINOR by hand (README, "Releasing"); builds after a release
# go on counting REVISION from there.
set -euo pipefail
cd "$(dirname "$0")/.."

note="${1:-}"
if [ -z "$note" ]; then
	echo "usage: tools/bump_version.sh \"what changed in this build\"" >&2
	exit 2
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
	echo "bump_version: commit or set aside your changes first (the build must match a commit)" >&2
	exit 1
fi

new=$(python3 - "$note" <<'EOF'
import datetime, json, re, sys
note = sys.argv[1]
text = open("plugin.json").read()
old = json.loads(text)["version"]
parts = old.split(".")
if len(parts) != 3 or parts[0] != "2" or not all(p.isdigit() for p in parts):
    sys.exit("bump_version: plugin.json version %r is not 2.MINOR.REVISION" % old)
new = "%s.%s.%d" % (parts[0], parts[1], int(parts[2]) + 1)
text, n = re.subn(r'("version"\s*:\s*")%s(")' % re.escape(old), r"\g<1>%s\g<2>" % new, text, count=1)
if n != 1:
    sys.exit("bump_version: could not find the version line in plugin.json")
open("plugin.json", "w").write(text)

log = open("CHANGELOG.md").read()
line = "- %s (%s): %s\n" % (new, datetime.date.today().isoformat(), note)
head = "## Test builds\n\n"
if head in log:
    log = log.replace(head, head + line, 1)
else:
    # After the title, before the first release section.
    i = log.find("\n## ")
    i = len(log) if i < 0 else i + 1
    log = log[:i] + head + line + "\n" + log[i:]
open("CHANGELOG.md", "w").write(log)
print(new)
EOF
)

git add plugin.json CHANGELOG.md
git commit -q -m "Build $new: $note"
echo "version $new ($(git rev-parse --short HEAD))"
