#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""The shared streaming vectors (tests/stream_vectors/, doc/10 §9) through the
command line: `bovnar mux list` and `bovnar frames list` on every vector.

`mux list` must list the manifest's messages, in order and at their lengths,
and fail (exit 1, naming the error) exactly when the vector does. `frames
list` must report every document with its own outcome, `frame N: OK` or
`frame N: ERROR <label>`, the count, and fail when a document failed or the
framing did, naming the framing error. The manifest is read through the CLI
itself (`bovnar convert`), so the check needs nothing but the binary.

The CLI cannot set max_message or max_document_size: it lists messages and
frames of any size, so a vector that sets one is read further than the
manifest says, and only the messages or documents before its error are held
to it.

Usage: check_stream_vectors_cli.py <bovnar> <tests/stream_vectors> [emulator...]
"""

import json
import os
import re
import subprocess
import sys


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, timeout=120)
    return (p.returncode, p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))


def check_mux(cli, path, v):
    rc, out, err = run(cli + ["mux", "list", path])
    got = [(int(c), int(n)) for c, n in
           re.findall(r"^channel (\d+): (\d+) bytes$", out, re.M)]
    want = [(m["channel"], m["length"]) for m in v["messages"]]
    if v["max_message"]:
        return got[:len(want)] == want and rc in (0, 1)
    if got != want:
        return False
    if v["error"] == "none":
        return rc == 0 and "error" not in err
    return rc == 1 and re.search(r"error %s$" % re.escape(v["error"]), err,
                                 re.M) is not None


def check_frames(cli, path, v):
    rc, out, err = run(cli + ["frames", "list", path])
    got = re.findall(r"^frame (\d+): (OK|ERROR \w+)$", out, re.M)
    want = [(str(i), "OK" if d["error"] == "none" else "ERROR " + d["error"])
            for i, d in enumerate(v["documents"])]
    if v["max_document_size"]:
        return got[:len(want)] == want and rc in (0, 1)
    count = re.search(r"^(\d+) document\(s\)$", out, re.M)
    if got != want or not count or int(count.group(1)) != len(want):
        return False
    failed = any(d["error"] != "none" for d in v["documents"])
    if v["error"] != "none":
        return rc == 1 and ("frames list: %s after %d document(s)"
                            % (v["error"], len(want))) in err
    return rc == (1 if failed else 0)


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    cli = argv[3:] + [argv[1]]
    vdir = argv[2]
    rc, out, err = run(cli + ["convert", "--from", "bvnr", "--to", "json",
                              os.path.join(vdir, "manifest.bvnr")])
    # Exit 1 says the JSON is lossy: the manifest's symbols (mux, none, the
    # error labels) become strings, which is all this reads them as.
    if rc not in (0, 1) or not out.strip():
        print("bovnar convert of the manifest failed:\n" + err)
        return 1
    manifest = json.loads(out)
    bad = 0
    for name, v in manifest.items():
        path = os.path.join(vdir, v["file"])
        ok = (check_mux if v["kind"] == "mux" else check_frames)(cli, path, v)
        print("%-28s %s" % (name, "ok" if ok else "MISMATCH"))
        bad += not ok
    if len(manifest) < 25:
        print("the manifest lists %d vectors" % len(manifest))
        return 1
    print("%d vectors, %d mismatches" % (len(manifest), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
