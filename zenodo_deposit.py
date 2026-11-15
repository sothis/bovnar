#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
zenodo_deposit.py — archive a release on Zenodo without moving the wrong
concept pointer.

A Zenodo concept DOI resolves to the record most recently published as a new
version of that concept, and a published record cannot be deleted or pointed
back. The two concepts opened for 1.1.0 (source 10.5281/zenodo.21443008,
documentation 10.5281/zenodo.21443295) therefore stay closed at 2.2.0.
zenodo_concepts.json lists their record ids, and this script refuses to
extend them.

From here a release opens, or extends, narrower concepts:

  * specification — one concept per spec number (`1.2`, then `1.3`). The
    Zenodo version field is that number. The files are the normative
    specification and the grammar. A library bugfix does not publish one.
    The concept is opened the first time that spec number is deposited.
    A later correction of the same number is a new version only when the
    run passes --corrigendum.
  * reference implementation — one concept per library minor (`2.2`, then
    `2.3`, then `3.0`). The version field is the library triple, which is
    also the version of every binding. The files are the source tarball
    and the manuals that describe that implementation. Patches are new
    versions of that minor's concept, so the pointer means "latest bugfix
    of this minor" and nothing else.

The spec record's related identifier is the implementation concept of the
minor that carries that spec, which keeps resolving to the latest patch of
that minor. A document's `#!bovnar` number therefore keeps a parser: the
spec concept for the number, then that implementation concept.

What a version holds:

  * implementation — `bovnar-<version>-<count>-source.tar.xz` as the release
    job attached it, checked against that release's SHA256SUMS, plus
    README.md and every gen_html_docs.DOCS document that is not the
    specification or the grammar, with their PDFs from build/doc/pdf/.
  * specification — doc/03 and doc/12 and their PDFs, and nothing else.

The PDFs are built from the working tree, so the script refuses them unless
the documents it is about to archive and the two generators that render
them are what the tag has, and every PDF is newer than the tag's last
change to those inputs.

A concept that already exists is extended with new-version, and the record
id in the table has to still be the newest version of that concept. A
missing id creates a new deposit. It never falls through to another line
or to the closed archive. --publish on the production host writes the new
record and concept ids back into the table; that change is part of the
release. The sandbox never writes the table.

Publishing is final. Without --publish the script stops at drafts. A second
run reuses an open draft of the same title and version. Try the sandbox
first; it creates fresh concepts, since it has none of the production ids.
--make-test-base still clones the closed 2.2.0 pair for a sandbox
experiment, and those clones are not concepts this script will extend.

ORCID: creators carry the ORCID iD, so DataCite's auto-update can attach a
published version to the author's record. --orcid-prefer runs
orcid_prefer.py afterwards, for the records this run published.
--resave re-publishes an edit of records deposited before auto-update was
enabled; it adds a missing ORCID iD and changes nothing else.

The token is read from ZENODO_TOKEN (scopes deposit:write and
deposit:actions); sandbox and production tokens are separate.

Usage:
    python3 zenodo_deposit.py --self-test
    python3 zenodo_deposit.py --plan --tag v2.2.1
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host sandbox --tag v2.2.1
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --tag v2.2.1 \\
        --publish
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --tag v2.2.4 \\
        --corrigendum --publish
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --resave 23934686
"""
import argparse
import hashlib
import html
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)

from check_release_links import (GITHUB_DOWNLOAD_PREFIX, REPO, expected_assets,  # noqa: E402
                                 git, version_at)

HOSTS = {
    "zenodo": "https://zenodo.org/api",
    "sandbox": "https://sandbox.zenodo.org/api",
}
# The closed archive's last records. --make-test-base clones these into the
# sandbox; the deposit path does not extend them.
HISTORICAL_LATEST = {"source": 24250459, "docs": 24250462}
CONCEPTS_PATH = os.path.join(ROOT, "zenodo_concepts.json")
WEBSITE = "https://www.bovnar.io"
USER_AGENT = "bovnar-zenodo-deposit"
PDF_DIR = os.path.join(ROOT, "build", "doc", "pdf")
# The normative format. Everything else gen_html_docs.DOCS publishes describes
# the implementation at a library version and travels with that record.
SPEC_DOCS = ("03_bovnar_spec.md", "12_bovnar.ebnf")
# Keywords a new version inherits but that are wrong. 1.1.0 called the format
# a binary serialization; it is a text one. Once a version is published with the
# fix, its successors inherit it and the entry does nothing.
KEYWORD_FIXES = {"Binary Serialization": "Text Serialization"}
TABLE_KEYS = ("note", "closed_concepts", "closed_records",
              "archived_library_versions", "spec", "implementation")


class ZenodoError(RuntimeError):
    pass


def die(msg):
    raise SystemExit(f"zenodo_deposit.py: {msg}")


def concept_doi(concept_id):
    return f"10.5281/zenodo.{int(concept_id)}"


def library_line(version):
    parts = version.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        die(f"{version} is not a library X.Y.Z")
    return f"{parts[0]}.{parts[1]}"


def load_table(path):
    """The concept table. A record id that also sits in the closed archive is
    refused here, so a typo cannot extend that series."""
    try:
        with open(path, encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, ValueError) as e:
        die(f"cannot read {path}: {e}")
    for key in TABLE_KEYS:
        if key not in raw:
            die(f"{path} has no {key}")
    closed = set(raw["closed_concepts"]) | set(raw["closed_records"])
    for kind in ("spec", "implementation"):
        if not isinstance(raw[kind], dict):
            die(f"{path}: {kind} is not an object")
        for key, entry in raw[kind].items():
            if not isinstance(entry, dict):
                die(f"{path}: {kind} {key} is not a record/concept pair")
            for field in ("record", "concept"):
                if not isinstance(entry.get(field), int):
                    die(f"{path}: {kind} {key} has no integer {field}")
                if entry[field] in closed:
                    die(f"{path}: {kind} {key} {field} {entry[field]} is in "
                        f"the closed archive")
    return raw


def dump_table(table):
    out = {key: table[key] for key in TABLE_KEYS}
    return json.dumps(out, indent=2, ensure_ascii=False) + "\n"


def bound(entry, table, what):
    """Refuse a target that belongs to the closed archive."""
    if entry is None:
        return
    closed = set(table["closed_concepts"]) | set(table["closed_records"])
    for field in ("record", "concept"):
        if entry[field] in closed:
            die(f"{what} targets closed record {entry[field]}")


def plan_deposits(version, spec, table, corrigendum):
    """Which concepts this release may touch. No network, no default.

    The implementation concept is the library minor. It is created when the
    table has no id for that minor. The specification concept is created when
    the table has no id for that spec number. Once the number is there, a
    release extends it only with corrigendum, so a bugfix stays a library
    patch and the spec number does not move.
    """
    if not re.fullmatch(r"\d+\.\d+", spec):
        die(f"{spec} is not a spec major.minor")
    if version in table["archived_library_versions"]:
        die(f"{version} is already in the closed archive; it is not deposited again")
    line = library_line(version)
    impl = table["implementation"].get(line)
    bound(impl, table, f"implementation {line}")
    implementation = {
        "kind": "implementation",
        "key": line,
        "version": version,
        "record": None if impl is None else impl["record"],
        "concept": None if impl is None else impl["concept"],
    }
    spec_entry = table["spec"].get(spec)
    bound(spec_entry, table, f"spec {spec}")
    if corrigendum:
        if spec_entry is None:
            die(f"spec {spec} has no concept to correct; the first deposit of "
                f"that number opens it, without --corrigendum")
        specification = {
            "kind": "spec",
            "key": spec,
            "version": spec,
            "record": spec_entry["record"],
            "concept": spec_entry["concept"],
        }
    elif spec_entry is None:
        specification = {
            "kind": "spec",
            "key": spec,
            "version": spec,
            "record": None,
            "concept": None,
        }
    else:
        specification = None
    return {"implementation": implementation, "spec": specification}


def format_plan(plan):
    lines = []
    for kind in ("implementation", "spec"):
        action = plan[kind]
        if action is None:
            lines.append(f"{kind}: unchanged")
            continue
        target = ("new concept" if action["record"] is None
                  else f"record {action['record']} (concept {action['concept']})")
        lines.append(f"{kind}: {action['key']} version {action['version']} -> {target}")
    return "\n".join(lines)


class Zenodo:
    def __init__(self, base, token):
        self.base = base
        self.token = token

    def call(self, method, url, body=None, data=None, auth=True, ok=(200, 201, 202, 204)):
        if not url.startswith("http"):
            url = self.base + url
        headers = {"User-Agent": USER_AGENT, "Accept": "application/json"}
        if auth:
            headers["Authorization"] = f"Bearer {self.token}"
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        elif data is not None:
            headers["Content-Type"] = "application/octet-stream"
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=300) as resp:
                status, raw = resp.status, resp.read()
        except urllib.error.HTTPError as e:
            status, raw = e.code, e.read()
        if status not in ok:
            raise ZenodoError(f"{method} {url} -> HTTP {status}: "
                              f"{raw.decode('utf-8', 'replace')[:2000]}")
        if not raw:
            return None
        try:
            return json.loads(raw)
        except ValueError:
            return raw.decode("utf-8", "replace")


# ── What the release is ─────────────────────────────────────────────────────

def git_bytes(tag, path):
    out = subprocess.run(["git", "-C", ROOT, "show", f"{tag}:{path}"],
                         capture_output=True, check=False)
    if out.returncode != 0:
        raise SystemExit(f"zenodo_deposit.py: {path} is not in {tag}")
    return out.stdout


def citation_at(tag, path=None):
    """CITATION.cff as committed at the tag, or the file at `path` instead."""
    try:
        import yaml
    except ImportError:
        raise SystemExit("zenodo_deposit.py: needs PyYAML to read CITATION.cff")
    if path:
        with open(path, "rb") as fh:
            return yaml.safe_load(fh)
    return yaml.safe_load(git_bytes(tag, "CITATION.cff"))


def release_info(tag, citation=None):
    version = version_at(tag)
    if version is None or tag != f"v{version}":
        raise SystemExit(f"zenodo_deposit.py: {tag} does not carry project() "
                         f"version {version} in CMakeLists.txt")
    count = git("rev-list", "--count", tag)
    if git("rev-parse", "--is-shallow-repository") == "true" or not (count or "").isdigit():
        raise SystemExit("zenodo_deposit.py: needs a full clone to count commits")
    cff = citation_at(tag, citation)
    where = citation or f"CITATION.cff at {tag}"
    if str(cff.get("version")) != version:
        raise SystemExit(f"zenodo_deposit.py: {where} says version "
                         f"{cff.get('version')}, not {version}")
    return version, count, cff


def creators_from(cff):
    out = []
    for a in cff["authors"]:
        c = {"name": f"{a['family-names']}, {a['given-names']}"}
        if a.get("orcid"):
            c["orcid"] = a["orcid"].rsplit("/", 1)[-1]
        if a.get("affiliation"):
            c["affiliation"] = a["affiliation"]
        out.append(c)
    return out


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=300) as resp:
        return resp.read()


def source_files(tag, version, count, workdir):
    """The release's source tarball, checked against its SHA256SUMS."""
    name = next(n for n in expected_assets(version, count)
                if n.endswith("-source.tar.xz"))
    # GitHub's release assets, not the site path. /download/ is where the
    # website serves them after publish_web.sh, and it does not exist until
    # that upload; the bytes archived here are the ones the release job attached.
    sums = fetch(f"{GITHUB_DOWNLOAD_PREFIX}{tag}/SHA256SUMS").decode("utf-8")
    want = None
    for line in sums.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == name:
            want = parts[0].lower()
    if want is None:
        raise SystemExit(f"zenodo_deposit.py: {name} is not in {tag}'s SHA256SUMS")
    blob = fetch(f"{GITHUB_DOWNLOAD_PREFIX}{tag}/{name}")
    if hashlib.sha256(blob).hexdigest() != want:
        raise SystemExit(f"zenodo_deposit.py: {name} does not match SHA256SUMS")
    path = os.path.join(workdir, name)
    with open(path, "wb") as fh:
        fh.write(blob)
    return [(name, path)]


def docs_for(kind):
    """Filenames from gen_html_docs.DOCS that this kind of record archives."""
    from gen_html_docs import DOCS
    sources = []
    for src, *_rest in DOCS:
        normative = src in SPEC_DOCS
        if normative == (kind == "spec"):
            sources.append(src)
    return tuple(sources)


def docs_files(tag, workdir, pdf_dir, sources, readme):
    """The named documents from the tag, and their PDFs. README rides with the
    implementation record; the specification record does not carry it."""
    from gen_html_docs import DOCS

    by_src = {src: row for src, *row in ((d[0], *d[1:]) for d in DOCS)}
    missing = [src for src in sources if src not in by_src]
    if missing:
        die(f"{missing[0]} is not a document gen_html_docs.py publishes")
    # The PDFs come from the working tree, not the tag. They stand for the tag
    # only if what they were built from is the tag's, and they were built since.
    # The wiki drafts and the JOSS paper live in doc/ but go into no PDF.
    inputs = ["gen_doc_pdfs.py", "gen_html_docs.py"] + [f"doc/{src}" for src in sources]
    if subprocess.run(["git", "-C", ROOT, "diff", "--quiet", tag, "--"] + inputs).returncode:
        die(f"{', '.join(inputs)} differ from {tag}; the PDFs would not be the release's")
    newest_input = int(git("log", "-1", "--format=%ct", tag, "--", *inputs) or "0")

    files = []
    if readme:
        path = os.path.join(workdir, "README.md")
        with open(path, "wb") as fh:
            fh.write(git_bytes(tag, "README.md"))
        files.append(("README.md", path))
    for src in sources:
        path = os.path.join(workdir, src)
        with open(path, "wb") as fh:
            fh.write(git_bytes(tag, f"doc/{src}"))
        files.append((src, path))
        pdf_slug = by_src[src][1]
        pdf = os.path.join(pdf_dir, pdf_slug + ".pdf")
        if not os.path.exists(pdf):
            die(f"{pdf} is missing; run gen_doc_pdfs.py")
        if os.path.getmtime(pdf) < newest_input:
            die(f"{pdf} is older than {tag}'s last change to the docs; run gen_doc_pdfs.py")
        num = src.split("_", 1)[0]
        files.append((f"{num}-{pdf_slug}.pdf", pdf))
    return files


# ── Metadata ────────────────────────────────────────────────────────────────

def link(url, text=None):
    u = html.escape(url, quote=True)
    return f'<a href="{u}">{html.escape(text or url)}</a>'


def describe(kind, tag, version, cff, other_doi, line):
    if kind == "implementation":
        parts = [f"<p>Bovnar {html.escape(version)}, the reference implementation "
                 f"of library line {html.escape(line)}. This concept resolves to "
                 f"the newest patch of that line. The C library and every binding "
                 f"carry this version number.</p>",
                 f"<p>{html.escape(' '.join(cff['abstract'].split()))}</p>"]
        notes = f"RELEASE_NOTES_v{version}.md"
        if git("cat-file", "-e", f"{tag}:{notes}") is not None:
            parts.append("<p>Release notes: "
                         + link(f"https://github.com/{REPO}/blob/{tag}/{notes}") + "</p>")
        if other_doi:
            parts.append("<p>Specification: " + link(f"https://doi.org/{other_doi}") + "</p>")
    else:
        parts = [f"<p>Bovnar specification {html.escape(version)}, the normative "
                 f"text and the grammar. The reference implementation of the "
                 f"library line that carries this specification is "
                 + link(f"https://doi.org/{other_doi}") + ".</p>"]
    parts.append("<p>Website: " + link(WEBSITE) + "</p>")
    return "\n".join(parts)


def metadata_for(kind, draft_meta, tag, version, cff, other_doi, line):
    m = dict(draft_meta)
    # Read-only or per-draft keys Zenodo reports but does not take back.
    for k in ("doi", "prereserve_doi", "resource_type", "relations"):
        m.pop(k, None)
    if kind == "implementation":
        m["title"] = f"Bovnar {version}"
        m["upload_type"] = "software"
        m.pop("publication_type", None)
    else:
        m["title"] = f"Bovnar specification {version}"
        m["upload_type"] = "publication"
        m["publication_type"] = "softwaredocumentation"
    m["version"] = version
    m["publication_date"] = str(cff["date-released"])
    m["description"] = describe(kind, tag, version, cff, other_doi, line)
    m["creators"] = creators_from(cff)
    m["language"] = "eng"
    # The deposit API takes a license id. A copied record reports it as
    # {"id": "mit-license"}; sending that object back is rejected.
    lic = m.get("license")
    if isinstance(lic, dict):
        lic = lic.get("id")
    m["license"] = lic or "mit-license"
    m["access_right"] = m.get("access_right") or "open"
    m["keywords"] = [KEYWORD_FIXES.get(k, k) for k in m.get("keywords", [])]
    tag_url = f"https://github.com/{REPO}/tree/{tag}"
    if kind == "implementation":
        related = [
            {"identifier": tag_url, "relation": "isSupplementTo",
             "resource_type": "software"},
        ]
        if other_doi:
            related.append({"identifier": other_doi, "relation": "isDocumentedBy",
                            "resource_type": "publication-softwaredocumentation"})
    else:
        related = [
            {"identifier": other_doi, "relation": "documents",
             "resource_type": "software"},
            {"identifier": tag_url, "relation": "isSupplementTo",
             "resource_type": "software"},
        ]
    m["related_identifiers"] = related
    return m


# ── Zenodo steps ────────────────────────────────────────────────────────────

def latest_published(z, record_id):
    rec = z.call("GET", f"/records/{record_id}/versions/latest", auth=False)
    return rec


def open_draft(z, record_id):
    """The new-version draft of `record_id`, found or made.

    A second newversion while a draft is open fails with an unhelpful "Please
    remove all files first", and the published record's latest_draft link points
    back at the record itself. The open drafts do carry their concept, so a
    rerun -- the --publish after a review -- finds its draft there.
    """
    concept = z.call("GET", f"/deposit/depositions/{record_id}")["conceptrecid"]
    for d in z.call("GET", "/deposit/depositions?status=draft&size=100") or []:
        # status=draft also lists published records (state "done"); skip them.
        if (str(d.get("conceptrecid")) == str(concept) and not d.get("submitted")
                and d.get("state") == "unsubmitted"):
            print(f"    reusing open draft {d['id']}")
            return z.call("GET", d["links"]["self"])
    r = z.call("POST", f"/deposit/depositions/{record_id}/actions/newversion")
    url = r["links"].get("latest_draft") or r["links"]["self"]
    return z.call("GET", url)


def reserve_doi(z, draft):
    doi = (draft["metadata"].get("prereserve_doi") or {}).get("doi")
    if doi:
        return draft, doi
    meta = dict(draft["metadata"])
    meta["prereserve_doi"] = True
    meta.pop("doi", None)
    z.call("PUT", draft["links"]["self"], body={"metadata": meta})
    draft = z.call("GET", draft["links"]["self"])
    doi = (draft["metadata"].get("prereserve_doi") or {}).get("doi")
    if not doi:
        raise ZenodoError(f"draft {draft['id']} reserved no DOI")
    return draft, doi


def replace_files(z, draft, files):
    for f in z.call("GET", draft["links"]["files"]) or []:
        z.call("DELETE", f["links"]["self"])
    bucket = draft["links"]["bucket"]
    for name, path in files:
        with open(path, "rb") as fh:
            data = fh.read()
        z.call("PUT", f"{bucket}/{urllib.request.quote(name)}", data=data)
        print(f"    uploaded {name} ({len(data):,} bytes)")
    listed = sorted(f["filename"] for f in z.call("GET", draft["links"]["files"]) or [])
    if listed != sorted(n for n, _ in files):
        raise ZenodoError(f"draft {draft['id']} holds {listed} after the upload")


def resave(z, record_ids):
    """Publish an edit of each record, so Zenodo re-registers its DOI.

    DataCite's ORCID auto-update only acts on DOIs registered or updated after
    the author enabled it. A release deposited before that reaches ORCID once
    its DOIs are updated, and an edit published without changes is the update
    that alters nothing. The one change made is the ORCID iD from CITATION.cff
    on a creator that lacks it (the 1.1.0 records were deposited without one),
    since without it DataCite cannot attribute the DOI to the author at all.
    The DataCite timestamps before and after say whether the update happened.
    """
    orcids = {c["name"]: c["orcid"] for c in creators_from(citation_at("HEAD"))
              if c.get("orcid")}
    for rid in record_ids:
        before = datacite_updated(z, rid)
        d = z.call("POST", f"/deposit/depositions/{rid}/actions/edit")
        meta = dict(d["metadata"])
        for k in ("doi", "prereserve_doi"):
            meta.pop(k, None)
        for c in meta.get("creators", []):
            if not c.get("orcid") and c.get("name") in orcids:
                c["orcid"] = orcids[c["name"]]
                print(f"    {rid}: ORCID {c['orcid']} added to {c['name']}")
        z.call("PUT", d["links"]["self"], body={"metadata": meta})
        p = z.call("POST", d["links"]["publish"])
        print(f"==> {rid}: re-published {p.get('doi')}")
        if before is not None:
            after = before
            for _ in range(12):
                time.sleep(5)
                after = datacite_updated(z, rid)
                if after and after != before:
                    break
            print(f"    DataCite updated: {before} -> {after}")


def datacite_updated(z, record_id):
    """DataCite's `updated` for a production record's DOI; None on the sandbox,
    whose 10.5072 test DOIs DataCite does not hold."""
    if "sandbox" in z.base:
        return None
    doi = z.call("GET", f"/records/{record_id}", auth=False)["doi"]
    try:
        attrs = json.loads(fetch(f"https://api.datacite.org/dois/{doi}"))["data"]["attributes"]
    except (urllib.error.URLError, KeyError, ValueError):
        return None
    return attrs.get("updated")


def make_test_base(z):
    """Sandbox only: publish stand-ins of the closed 2.2.0 records.

    The deposit path does not extend these copies. A sandbox deposit opens
    fresh concepts of its own.
    """
    prod = HOSTS["zenodo"]
    made = {}
    for kind, rid in HISTORICAL_LATEST.items():
        rec = Zenodo(prod, None).call("GET", f"/records/{rid}", auth=False)
        m = rec["metadata"]
        rt = m["resource_type"]
        lic = m.get("license")
        if isinstance(lic, dict):
            lic = lic.get("id")
        meta = {
            "title": m["title"] + " (sandbox stand-in)",
            "version": m["version"],
            "publication_date": m["publication_date"],
            "description": m["description"],
            "creators": [{"name": c["name"]} for c in m["creators"]],
            "keywords": m.get("keywords", []),
            # The deposit API takes the license id. "MIT" is rejected.
            "license": lic or "mit-license",
            "upload_type": rt["type"],
            "access_right": "open",
        }
        if rt.get("subtype"):
            meta[f"{rt['type']}_type"] = rt["subtype"]
        d = z.call("POST", "/deposit/depositions", body={})
        z.call("PUT", f"{d['links']['bucket']}/placeholder-{kind}-{m['version']}.txt",
               data=f"stand-in for zenodo record {rid}\n".encode())
        z.call("PUT", d["links"]["self"], body={"metadata": meta})
        p = z.call("POST", d["links"]["publish"])
        made[kind] = p["id"]
        print(f"  {kind}: record {p['id']}, DOI {p.get('doi')}")
    print(f"stand-ins of the closed 2.2.0 records: "
          f"source {made['source']}, docs {made['docs']}")
    print("A sandbox deposit opens fresh concepts and does not extend these.")


def spec_at(tag):
    text = git("show", f"{tag}:include/bovnar.h")
    if not text:
        die(f"{tag} has no include/bovnar.h")
    found = {}
    for name in ("MAJOR", "MINOR"):
        m = re.search(rf"^[ \t]*#define[ \t]+BVNR_SPEC_VERSION_{name}[ \t]+(\d+)\b",
                      text, re.M)
        if not m:
            die(f"{tag} does not define BVNR_SPEC_VERSION_{name}")
        found[name] = m.group(1)
    return f"{found['MAJOR']}.{found['MINOR']}"


def version_strings(record):
    """Version labels already published on this concept."""
    data = record if isinstance(record, list) else None
    hits = []
    if isinstance(record, dict):
        hits = record.get("hits", {}).get("hits", [])
        if not hits and "metadata" in record:
            hits = [record]
    elif data is not None:
        hits = data
    out = []
    for hit in hits:
        meta = hit.get("metadata") or {}
        if meta.get("version"):
            out.append(str(meta["version"]))
    return out


def extend_state(z, action):
    """'new', 'extend', or 'done'.

    A library version already on its concept is done, so a rerun that
    published the implementation and then failed on the specification can
    finish the specification. A spec corrigendum keeps the spec number as
    its version label, so that label is allowed to recur there.
    """
    if action["record"] is None:
        return "new", None
    latest = latest_published(z, action["record"])
    if int(latest["id"]) != int(action["record"]):
        die(f"{action['kind']} {action['key']} record {action['record']} is not "
            f"the newest version ({latest['id']} is); update zenodo_concepts.json")
    concept = latest.get("conceptrecid") or action["concept"]
    if concept is not None and int(concept) != int(action["concept"]):
        die(f"{action['kind']} {action['key']} record {action['record']} belongs "
            f"to concept {concept}, not {action['concept']}")
    listing = z.call("GET", f"/records/{action['record']}/versions?size=100",
                     auth=False)
    if isinstance(listing, dict):
        hits = listing.get("hits", {})
        total = hits.get("total") if isinstance(hits, dict) else None
        seen = len(hits.get("hits", [])) if isinstance(hits, dict) else 0
        if isinstance(total, int) and total > seen:
            die(f"{action['kind']} {action['key']} has {total} versions; "
                f"refusing to guess whether {action['version']} is already published")
    if (action["kind"] == "implementation"
            and action["version"] in version_strings(listing)):
        print(f"==> implementation {action['key']} already has {action['version']}: "
              f"{latest.get('doi')}")
        return "done", latest.get("doi")
    return "extend", None


def find_new_draft(z, title, version):
    """An unpublished deposit already opened for this title and version."""
    listed = z.call("GET", "/deposit/depositions?status=draft&size=100") or []
    for brief in listed:
        if brief.get("submitted") or brief.get("state") != "unsubmitted":
            continue
        draft = z.call("GET", brief["links"]["self"])
        meta = draft.get("metadata") or {}
        if meta.get("title") == title and str(meta.get("version")) == version:
            print(f"    reusing open draft {draft['id']}")
            return draft
    return None


def open_new_draft(z, title, version, creators, upload_type):
    found = find_new_draft(z, title, version)
    if found:
        return found
    draft = z.call("POST", "/deposit/depositions", body={})
    meta = {
        "title": title,
        "upload_type": upload_type,
        "description": f"<p>{html.escape(title)}</p>",
        "creators": creators,
        "version": version,
        "license": "mit-license",
        "access_right": "open",
    }
    if upload_type == "publication":
        meta["publication_type"] = "softwaredocumentation"
    z.call("PUT", draft["links"]["self"], body={"metadata": meta})
    return z.call("GET", draft["links"]["self"])


def draft_title(action):
    if action["kind"] == "implementation":
        return f"Bovnar {action['version']}"
    return f"Bovnar specification {action['version']}"


def remember(table, action, published, draft):
    concept = (published.get("conceptrecid") or draft.get("conceptrecid")
               or action["concept"])
    record = published.get("id") or draft.get("id")
    if concept is None or record is None:
        die(f"{action['kind']} {action['key']} published without a record id")
    table[action["kind"]][action["key"]] = {
        "record": int(record),
        "concept": int(concept),
    }


def self_test():
    """The deposit rules, with no network and no token."""
    table = load_table(CONCEPTS_PATH)
    for concept in (21443008, 21443295):
        if concept not in table["closed_concepts"]:
            die(f"self-test: closed concept {concept} is not in the table")
    fresh = json.loads(json.dumps(table))
    fresh["spec"] = {}
    fresh["implementation"] = {}

    def refused(version, spec, concepts, corrigendum, text):
        try:
            plan_deposits(version, spec, concepts, corrigendum)
        except SystemExit as exc:
            if text not in str(exc):
                die(f"self-test: {version} refused with {exc}, wanted {text!r}")
            return
        die(f"self-test: {version} corrigendum={corrigendum} was accepted")

    refused("2.2.0", "1.2", fresh, False, "2.2.0")
    plan = plan_deposits("2.2.1", "1.2", fresh, False)
    if plan["implementation"]["record"] is not None or plan["implementation"]["key"] != "2.2":
        die(f"self-test: 2.2.1 implementation plan is {plan['implementation']}")
    if plan["implementation"]["version"] != "2.2.1":
        die("self-test: implementation version is not the library triple")
    if plan["spec"] is None or plan["spec"]["record"] is not None or plan["spec"]["version"] != "1.2":
        die(f"self-test: 2.2.1 spec plan is {plan['spec']}")

    opened = json.loads(json.dumps(fresh))
    opened["spec"]["1.2"] = {"record": 11, "concept": 10}
    opened["implementation"]["2.2"] = {"record": 21, "concept": 20}
    plan = plan_deposits("2.2.2", "1.2", opened, False)
    if plan["spec"] is not None or plan["implementation"]["record"] != 21:
        die(f"self-test: a patch extended the spec: {plan}")
    plan = plan_deposits("3.0.0", "1.2", opened, False)
    if (plan["spec"] is not None or plan["implementation"]["key"] != "3.0"
            or plan["implementation"]["record"] is not None):
        die(f"self-test: an ABI major of the same spec reused a concept: {plan}")
    plan = plan_deposits("2.3.0", "1.3", opened, False)
    if (plan["implementation"]["key"] != "2.3" or plan["implementation"]["record"] is not None
            or plan["spec"]["key"] != "1.3" or plan["spec"]["record"] is not None):
        die(f"self-test: a spec move reused an old concept: {plan}")
    plan = plan_deposits("2.2.3", "1.2", opened, True)
    if plan["spec"]["record"] != 11 or plan["spec"]["version"] != "1.2":
        die(f"self-test: corrigendum did not extend spec 1.2: {plan}")
    if plan["implementation"]["record"] != 21:
        die("self-test: corrigendum left the implementation line")
    refused("2.3.1", "1.3", opened, True, "1.3")
    bad = json.loads(json.dumps(opened))
    bad["implementation"]["2.2"] = {"record": 24250459, "concept": 21443008}
    refused("2.2.4", "1.2", bad, False, "24250459")

    from gen_html_docs import DOCS
    names = [row[0] for row in DOCS]
    if SPEC_DOCS != ("03_bovnar_spec.md", "12_bovnar.ebnf"):
        die(f"self-test: SPEC_DOCS is {SPEC_DOCS}")
    for src in SPEC_DOCS:
        if src not in names:
            die(f"self-test: {src} is not a published document")
    impl = docs_for("implementation")
    spec_docs = docs_for("spec")
    if set(impl) & set(spec_docs) or set(impl) | set(spec_docs) != set(names):
        die("self-test: spec and implementation documents do not partition DOCS")

    page = {"hits": {"hits": [
        {"metadata": {"version": "2.2.1"}, "conceptrecid": 20},
        {"metadata": {"version": "2.2.0"}},
    ]}}
    if version_strings(page) != ["2.2.1", "2.2.0"]:
        die(f"self-test: version page parsed as {version_strings(page)}")
    if version_strings([{"metadata": {"version": "1.2"}}]) != ["1.2"]:
        die("self-test: a version list did not yield its labels")
    if version_strings({"metadata": {"version": "1.2"}}) != ["1.2"]:
        die("self-test: a single record did not yield its version")

    cff = {
        "abstract": "A text format.",
        "date-released": "2026-11-15",
        "authors": [{"family-names": "Sonntag", "given-names": "Janos",
                     "orcid": "https://orcid.org/0009-0008-1299-6534"}],
    }
    copied = {"license": {"id": "mit-license"},
              "keywords": ["Binary Serialization"],
              "doi": "10.5281/zenodo.1",
              "resource_type": {"type": "software"},
              "relations": {"version": []}}
    meta = metadata_for("implementation", copied, "v9.9.9", "9.9.9", cff,
                        "10.5281/zenodo.10", "9.9")
    if meta["license"] != "mit-license" or meta["upload_type"] != "software":
        die(f"self-test: implementation metadata is {meta['license']!r}, "
            f"{meta['upload_type']!r}")
    if any(k in meta for k in ("doi", "resource_type", "relations")):
        die("self-test: metadata echoed a read-only key")
    if meta["keywords"] != ["Text Serialization"]:
        die(f"self-test: keyword fix yielded {meta['keywords']}")
    documented = [r for r in meta["related_identifiers"]
                  if r["relation"] == "isDocumentedBy"]
    if documented != [{"identifier": "10.5281/zenodo.10",
                       "relation": "isDocumentedBy",
                       "resource_type": "publication-softwaredocumentation"}]:
        die(f"self-test: implementation does not point at the spec: {documented}")
    spec_meta = metadata_for("spec", {"license": "mit-license"}, "v9.9.9", "1.2",
                             cff, "10.5281/zenodo.20", "9.9")
    if spec_meta["title"] != "Bovnar specification 1.2":
        die(f"self-test: spec title is {spec_meta['title']!r}")
    if (spec_meta["upload_type"] != "publication"
            or spec_meta["publication_type"] != "softwaredocumentation"):
        die("self-test: spec upload type is not a software document")
    if spec_meta["related_identifiers"][0] != {
            "identifier": "10.5281/zenodo.20", "relation": "documents",
            "resource_type": "software"}:
        die("self-test: spec does not point at the implementation concept")
    print("zenodo_deposit.py: self-test ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--host", choices=sorted(HOSTS))
    ap.add_argument("--tag", help="release tag (required to deposit or plan)")
    ap.add_argument("--concepts", default=CONCEPTS_PATH,
                    help="concept table (default: zenodo_concepts.json)")
    ap.add_argument("--pdf-dir", default=PDF_DIR)
    # v2.0.1 was tagged with CITATION.cff still at version 2.0.0; a tag cannot
    # be moved once its release is published, so the file is read from here.
    ap.add_argument("--citation", metavar="FILE",
                    help="read CITATION.cff from FILE instead of from the tag "
                         "(for a tag cut with a stale one); its version must still "
                         "be the tag's")
    ap.add_argument("--corrigendum", action="store_true",
                    help="also publish a new version of this tag's spec concept")
    ap.add_argument("--publish", action="store_true", help="publish the drafts (final)")
    ap.add_argument("--plan", action="store_true",
                    help="print which concepts this tag would touch, and stop")
    ap.add_argument("--self-test", action="store_true",
                    help="check the deposit rules and exit")
    ap.add_argument("--make-test-base", action="store_true",
                    help="sandbox only: publish stand-ins of the closed 2.2.0 pair")
    ap.add_argument("--resave", type=int, nargs="+", metavar="RECORD",
                    help="publish an edit of these records (adding a missing creator "
                         "ORCID), so their DOIs reach ORCID through DataCite's auto-update")
    ap.add_argument("--orcid-prefer", action="store_true",
                    help="after --publish, make the new works the ORCID preferred "
                         "source and featured works (needs the session cookie file; "
                         "see orcid_prefer.py)")
    ap.add_argument("--orcid-wait", type=float, default=600.0, metavar="SECONDS",
                    help="with --orcid-prefer, wait this long for DataCite's "
                         "auto-update to reach ORCID (default: 600)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if args.plan:
        if not args.tag:
            die("--plan needs --tag vX.Y.Z")
        version, _count, _cff = release_info(args.tag, args.citation)
        plan = plan_deposits(version, spec_at(args.tag), load_table(args.concepts),
                             args.corrigendum)
        print(f"==> {args.tag}: library {version}, spec {spec_at(args.tag)}")
        print(format_plan(plan))
        return 0

    if not args.host:
        die("--host is required")
    if args.orcid_prefer and not args.publish:
        die("--orcid-prefer needs --publish")
    if args.orcid_prefer and args.host != "zenodo":
        die("--orcid-prefer is for the production host")

    token = os.environ.get("ZENODO_TOKEN")
    if not token:
        die("set ZENODO_TOKEN")
    z = Zenodo(HOSTS[args.host], token)

    if args.make_test_base:
        if args.host != "sandbox":
            die("--make-test-base is for the sandbox only")
        make_test_base(z)
        return 0
    if args.resave:
        resave(z, args.resave)
        return 0
    if not args.tag:
        die("pass --tag vX.Y.Z; a maintenance release is not the newest tag")

    table = load_table(args.concepts)
    # The sandbox has none of the production concepts. Depositing there always
    # opens fresh ones, and the production table is left untouched.
    if args.host == "sandbox":
        table = json.loads(json.dumps(table))
        table["spec"] = {}
        table["implementation"] = {}
    version, count, cff = release_info(args.tag, args.citation)
    spec = spec_at(args.tag)
    plan = plan_deposits(version, spec, table, args.corrigendum)
    print(f"==> {args.tag}: library {version}, spec {spec}, build count {count}, "
          f"host {args.host}")
    print(format_plan(plan))

    actions = [plan["implementation"]]
    if plan["spec"] is not None:
        actions.append(plan["spec"])
    published = {}
    pending = []
    for action in actions:
        state, doi = extend_state(z, action)
        if state == "done":
            published[action["kind"]] = doi
            continue
        pending.append(action)
    if not pending:
        print("==> nothing to deposit")
        if not (args.orcid_prefer and published.get("implementation")):
            return 0
    actions = pending

    creators = creators_from(cff)
    impl_line = plan["implementation"]["key"]
    drafts, dois = {}, {}
    if actions:
        with tempfile.TemporaryDirectory(prefix="bovnar-zenodo-") as work:
            files = {}
            wanted = {action["kind"] for action in actions}
            if "implementation" in wanted:
                files["implementation"] = (
                    source_files(args.tag, version, count, work)
                    + docs_files(args.tag, work, args.pdf_dir,
                                 docs_for("implementation"), readme=True))
            if "spec" in wanted:
                files["spec"] = docs_files(args.tag, work, args.pdf_dir,
                                           docs_for("spec"), readme=False)

            for action in actions:
                if action["record"] is None:
                    draft = open_new_draft(
                        z, draft_title(action), action["version"], creators,
                        "software" if action["kind"] == "implementation" else "publication")
                else:
                    draft = open_draft(z, action["record"])
                draft, doi = reserve_doi(z, draft)
                drafts[action["kind"]], dois[action["kind"]] = draft, doi
                # A new deposition carries conceptrecid before it is published.
                # The cross-link has to be that concept: a version DOI would
                # pin one patch, and the spec would stop finding later bugfixes.
                concept = draft.get("conceptrecid") or action["concept"]
                if concept is None:
                    die(f"{action['kind']} draft {draft['id']} has no concept id")
                action["draft_concept"] = int(concept)
                print(f"==> {action['kind']}: draft {draft['id']}, "
                      f"concept {action['draft_concept']}, DOI {doi}")

            impl_concept = (plan["implementation"]["concept"]
                            or plan["implementation"].get("draft_concept"))
            if not impl_concept:
                die("no implementation concept to link")
            impl_link = concept_doi(impl_concept)
            spec_action = plan["spec"]
            spec_concept = None
            if spec_action is not None:
                spec_concept = (spec_action["concept"]
                                or spec_action.get("draft_concept"))
            elif table["spec"].get(spec):
                spec_concept = table["spec"][spec]["concept"]
            spec_link = concept_doi(spec_concept) if spec_concept else None

            for action in actions:
                kind = action["kind"]
                print(f"==> {kind}: files")
                replace_files(z, drafts[kind], files[kind])
                other = spec_link if kind == "implementation" else impl_link
                meta = metadata_for(kind, drafts[kind]["metadata"], args.tag,
                                    action["version"], cff, other, impl_line)
                z.call("PUT", drafts[kind]["links"]["self"], body={"metadata": meta})
                drafts[kind] = z.call("GET", drafts[kind]["links"]["self"])

    for action in actions:
        kind = action["kind"]
        draft = drafts[kind]
        if args.publish:
            response = z.call("POST", draft["links"]["publish"])
            published[kind] = response.get("doi") or dois[kind]
            page = response["links"].get("html") or response["links"].get("record_html", "")
            print(f"==> {kind}: published {response.get('doi')}  {page}")
            # The sandbox reserves 10.5281/... and mints 10.5072/...
            if response.get("doi") != dois[kind]:
                print(f"    WARNING: reserved {dois[kind]} but published as "
                      f"{response.get('doi')}; the other record's link to it is wrong")
            # Record this concept before the next publish. A failure on the
            # specification then leaves the implementation id in the table,
            # and a rerun skips the version that is already there.
            if args.host == "zenodo":
                remember(table, action, response, draft)
                with open(args.concepts, "w", encoding="utf-8") as fh:
                    fh.write(dump_table(table))
                print(f"==> recorded {kind} in {args.concepts}; commit that change")
            time.sleep(1)
        else:
            print(f"==> {kind}: draft ready, DOI {dois[kind]} on publish  "
                  f"{draft['links'].get('html', '')}")
    if not args.publish:
        print("Review the drafts, then run again with --publish.")
        return 0
    if args.host != "zenodo":
        print("==> sandbox concepts are not written to the table")

    if args.orcid_prefer and published.get("implementation"):
        print("==> ORCID: making the new works the preferred source")
        cmd = [sys.executable, os.path.join(ROOT, "orcid_prefer.py"),
               "--tag", args.tag,
               "--source-doi", published["implementation"],
               "--wait", str(args.orcid_wait)]
        if "spec" in published:
            cmd += ["--docs-doi", published["spec"]]
        else:
            cmd += ["--only", "source"]
        if subprocess.run(cmd).returncode:
            print("    ORCID preference not set; fix the cookie (or run "
                  "orcid_prefer.py later) and rerun that step.", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ZenodoError as e:
        sys.exit(f"zenodo_deposit.py: {e}")
