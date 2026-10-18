#!/usr/bin/env bash
set -euo pipefail

loader_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upstream_dir="$loader_dir/upstream"
output_dir="${1:-$loader_dir/build-output}"

if [[ -z "${DEVKITPRO:-}" || ! -f "$DEVKITPRO/libnx/switch_rules" ]]; then
    echo "Set DEVKITPRO to a devkitPro installation containing libnx/switch_rules." >&2
    exit 2
fi

mkdir -p "$output_dir"
make -C "$upstream_dir" RELEASE=1 clean
make -C "$upstream_dir" RELEASE=1 CONFIG_JSON=hbl.json

python_command="${PYTHON:-python3}"
if ! command -v "$python_command" >/dev/null 2>&1; then
    python_command=python
fi
"$python_command" - "$upstream_dir/hbl.nsp" "$output_dir" <<'PY'
import pathlib
import struct
import sys

package = pathlib.Path(sys.argv[1]).read_bytes()
out_dir = pathlib.Path(sys.argv[2])
if package[:4] != b"PFS0":
    raise SystemExit("nx-hbloader build did not produce a PFS0 NSP")

count, string_size = struct.unpack_from("<II", package, 4)
entries_start = 0x10
strings_start = entries_start + count * 0x18
data_start = strings_start + string_size
if data_start > len(package):
    raise SystemExit("invalid PFS0 header in nx-hbloader NSP")

files = {}
for index in range(count):
    entry = entries_start + index * 0x18
    offset, size, name_offset = struct.unpack_from("<QQI", package, entry)
    if name_offset >= string_size:
        raise SystemExit("invalid PFS0 filename offset")
    name_start = strings_start + name_offset
    name_end = package.find(b"\0", name_start, strings_start + string_size)
    if name_end < 0:
        raise SystemExit("unterminated PFS0 filename")
    name = package[name_start:name_end].decode("ascii")
    start, end = data_start + offset, data_start + offset + size
    if end > len(package):
        raise SystemExit(f"PFS0 entry out of bounds: {name}")
    files[name] = package[start:end]

for source_name, output_name, magic in (
    ("main", "main", b"NSO0"),
    ("main.npdm", "main.npdm", b"META"),
):
    data = files.get(source_name)
    if not data or data[:4] != magic:
        raise SystemExit(f"missing or invalid {source_name} in nx-hbloader NSP")
    (out_dir / output_name).write_bytes(data)

print(f"Built matching loader files in {out_dir}")
PY
