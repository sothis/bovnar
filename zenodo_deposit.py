#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
"""
zenodo_deposit.py — archive a release on Zenodo as a new version of its two
records: the source, and the documentation and specification.

Bovnar has two Zenodo concept DOIs, each a line of versions:

  * source         10.5281/zenodo.21443008 (1.1.0: record 21443009)
  * documentation  10.5281/zenodo.21443295 (1.1.0: record 21443296)

A release adds one version to each, so the concept DOIs keep resolving to the
newest release and every release gets a version DOI of its own. Zenodo's GitHub
integration is not used: it would start a third concept, archive GitHub's
snapshot zip instead of the release's own source tarball, and cannot make the
documentation record at all.

What a version holds:

  * source  — `bovnar-<version>-<count>-source.tar.xz` as the release job
    attached it to the GitHub release, checked against that release's
    SHA256SUMS, so the archived bytes are the downloaded bytes.
  * docs    — README.md and every document gen_html_docs.DOCS publishes, read
    from the tag, plus their PDFs from build/doc/pdf/ (gen_doc_pdfs.py), named
    with the document's number as in the website's zip. The PDFs are built
    from the working tree, so the script refuses them unless doc/ and the two
    generators that render it are what the tag has, and every PDF is newer
    than the tag's last change to them.

The new version starts as a copy of the previous one (Zenodo copies its
metadata), and only what belongs to the release is set: title, version,
publication date (CITATION.cff's date-released at the tag), description,
creators (with the ORCID from CITATION.cff), language and the related
identifiers — the GitHub tag, and each record pointing at the other. License,
resource type and keywords stay as the previous version has them, except for
the keywords KEYWORD_FIXES corrects.

Publishing is final: a published Zenodo record cannot be deleted and its files
cannot be replaced. Without --publish the script stops at two drafts and prints
their pages to review; run it again with --publish to publish them (the drafts
are reused, their files uploaded afresh). Try it on sandbox.zenodo.org first:
sandbox has no copy of the production records, so --make-test-base creates and
publishes a stand-in 1.1.0 pair there to make a new version of.

ORCID: the creators carry the ORCID iD, so with DataCite's ORCID auto-update
enabled (DataCite Commons, signed in with ORCID) each published version reaches
the author's ORCID record by itself. The preferred source does not move with
it -- ORCID keeps the version the researcher selected -- and its API cannot
raise another client's work, so --orcid-prefer runs orcid_prefer.py with the
record owner's session cookie after publishing. --resave publishes an edit of
records deposited before auto-update was enabled, which pushes them too; it
adds the ORCID iD to a creator that has none, and changes nothing else.

The token is read from ZENODO_TOKEN (scopes deposit:write and deposit:actions);
sandbox and production tokens are separate.

Usage:
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host sandbox --make-test-base
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host sandbox \\
        --source-record <id> --docs-record <id> [--publish]
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo [--tag v2.0.2] [--publish]
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --tag v2.1.0 --publish --orcid-prefer
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --resave 23934686 23934688
    ZENODO_TOKEN=... python3 zenodo_deposit.py --host zenodo --resave 21443009 21443296
"""
import argparse
import hashlib
import html
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)

from check_release_links import (DOWNLOAD_PREFIX, REPO, expected_assets,  # noqa: E402
                                 git, newest_tag, version_at)

HOSTS = {
    "zenodo": "https://zenodo.org/api",
    "sandbox": "https://sandbox.zenodo.org/api",
}
# The latest version on each production line, i.e. the record a new version is
# made from. A sandbox run names its own with --source-record/--docs-record.
PRODUCTION_RECORDS = {"source": 23963398, "docs": 23963399}
WEBSITE = "https://www.bovnar.io"
USER_AGENT = "bovnar-zenodo-deposit"
PDF_DIR = os.path.join(ROOT, "build", "doc", "pdf")
# Keywords a new version inherits but that are wrong. 1.1.0 called the format
# a binary serialization; it is a text one. Once a version is published with the
# fix, its successors inherit it and the entry does nothing.
KEYWORD_FIXES = {"Binary Serialization": "Text Serialization"}


class ZenodoError(RuntimeError):
    pass


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
    sums = fetch(f"{DOWNLOAD_PREFIX}{tag}/SHA256SUMS").decode("utf-8")
    want = None
    for line in sums.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == name:
            want = parts[0].lower()
    if want is None:
        raise SystemExit(f"zenodo_deposit.py: {name} is not in {tag}'s SHA256SUMS")
    blob = fetch(f"{DOWNLOAD_PREFIX}{tag}/{name}")
    if hashlib.sha256(blob).hexdigest() != want:
        raise SystemExit(f"zenodo_deposit.py: {name} does not match SHA256SUMS")
    path = os.path.join(workdir, name)
    with open(path, "wb") as fh:
        fh.write(blob)
    return [(name, path)]


def docs_files(tag, workdir, pdf_dir):
    """README.md, the published documents from the tag, and their PDFs."""
    from gen_html_docs import DOCS

    # The PDFs come from the working tree, not the tag. They stand for the tag
    # only if what they were built from is the tag's, and they were built since.
    # README.md is not among them: it has no PDF, and its deposited copy is
    # read from the tag like the Markdown documents.
    # The wiki drafts and the JOSS paper live in doc/ but go into no PDF; the
    # wiki infobox names the newest release and so changes after every tag.
    inputs = ["doc", ":(exclude)doc/wiki", ":(exclude)doc/joss_paper",
              "gen_doc_pdfs.py", "gen_html_docs.py"]
    if subprocess.run(["git", "-C", ROOT, "diff", "--quiet", tag, "--"] + inputs).returncode:
        raise SystemExit(f"zenodo_deposit.py: doc/, gen_doc_pdfs.py or gen_html_docs.py "
                         f"differ from {tag}; the PDFs would not be the release's")
    newest_input = int(git("log", "-1", "--format=%ct", tag, "--", *inputs))

    files = []
    path = os.path.join(workdir, "README.md")
    with open(path, "wb") as fh:
        fh.write(git_bytes(tag, "README.md"))
    files.append(("README.md", path))
    for src, _slug, pdf_slug, *_ in DOCS:
        path = os.path.join(workdir, src)
        with open(path, "wb") as fh:
            fh.write(git_bytes(tag, f"doc/{src}"))
        files.append((src, path))
    for src, _slug, pdf_slug, *_ in DOCS:
        pdf = os.path.join(pdf_dir, pdf_slug + ".pdf")
        if not os.path.exists(pdf):
            raise SystemExit(f"zenodo_deposit.py: {pdf} is missing; run gen_doc_pdfs.py")
        if os.path.getmtime(pdf) < newest_input:
            raise SystemExit(f"zenodo_deposit.py: {pdf} is older than {tag}'s last "
                             f"change to the docs; run gen_doc_pdfs.py")
        num = src.split("_", 1)[0]
        files.append((f"{num}-{pdf_slug}.pdf", pdf))
    return files


# ── Metadata ────────────────────────────────────────────────────────────────

def link(url, text=None):
    u = html.escape(url, quote=True)
    return f'<a href="{u}">{html.escape(text or url)}</a>'


def describe(kind, tag, version, cff, other_doi):
    if kind == "source":
        parts = [f"<p>Bovnar {html.escape(version)}</p>",
                 f"<p>{html.escape(' '.join(cff['abstract'].split()))}</p>"]
        notes = f"RELEASE_NOTES_v{version}.md"
        if git("cat-file", "-e", f"{tag}:{notes}") is not None:
            parts.append("<p>Release notes: "
                         + link(f"https://github.com/{REPO}/blob/{tag}/{notes}") + "</p>")
        parts.append("<p>Documentation and specification of this release: "
                     + link(f"https://doi.org/{other_doi}") + "</p>")
    else:
        parts = [f"<p>Bovnar {html.escape(version)} Documentation and Specification of "
                 "the published source tarball ("
                 + link(f"https://doi.org/{other_doi}")
                 + "), as the Markdown sources and as PDF.</p>"]
    parts.append("<p>Website: " + link(WEBSITE) + "</p>")
    return "\n".join(parts)


def metadata_for(kind, draft_meta, tag, version, cff, own_doi, other_doi):
    m = dict(draft_meta)
    # Read-only or per-draft keys Zenodo reports but does not take back.
    for k in ("doi", "prereserve_doi"):
        m.pop(k, None)
    m["title"] = (f"Bovnar {version}" if kind == "source"
                  else f"Bovnar {version} Documentation and Specification")
    m["version"] = version
    m["publication_date"] = str(cff["date-released"])
    m["description"] = describe(kind, tag, version, cff, other_doi)
    m["creators"] = creators_from(cff)
    m["language"] = "eng"
    m["keywords"] = [KEYWORD_FIXES.get(k, k) for k in m.get("keywords", [])]
    tag_url = f"https://github.com/{REPO}/tree/{tag}"
    if kind == "source":
        m["related_identifiers"] = [
            {"identifier": tag_url, "relation": "isSupplementTo", "resource_type": "software"},
            {"identifier": other_doi, "relation": "isDocumentedBy",
             "resource_type": "publication-softwaredocumentation"},
        ]
    else:
        m["related_identifiers"] = [
            {"identifier": other_doi, "relation": "documents", "resource_type": "software"},
            {"identifier": tag_url, "relation": "isSupplementTo", "resource_type": "software"},
        ]
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
    """Sandbox only: publish a stand-in for the production 1.1.0 pair."""
    prod = HOSTS["zenodo"]
    made = {}
    for kind, rid in PRODUCTION_RECORDS.items():
        rec = Zenodo(prod, None).call("GET", f"/records/{rid}", auth=False)
        m = rec["metadata"]
        rt = m["resource_type"]
        meta = {
            "title": m["title"] + " (sandbox stand-in)",
            "version": m["version"],
            "publication_date": m["publication_date"],
            "description": m["description"],
            "creators": [{"name": c["name"]} for c in m["creators"]],
            "keywords": m.get("keywords", []),
            "license": {"mit-license": "MIT"}.get(m["license"]["id"], m["license"]["id"]),
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
    print(f"\nnext: --source-record {made['source']} --docs-record {made['docs']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--host", choices=sorted(HOSTS), required=True)
    ap.add_argument("--tag", help="release tag (default: the newest v* tag)")
    ap.add_argument("--source-record", type=int, help="record to make the new source version of")
    ap.add_argument("--docs-record", type=int, help="record to make the new docs version of")
    ap.add_argument("--pdf-dir", default=PDF_DIR)
    # v2.0.1 was tagged with CITATION.cff still at version 2.0.0; a tag cannot
    # be moved once its release is published, so the file is read from here.
    ap.add_argument("--citation", metavar="FILE",
                    help="read CITATION.cff from FILE instead of from the tag "
                         "(for a tag cut with a stale one); its version must still "
                         "be the tag's")
    ap.add_argument("--publish", action="store_true", help="publish the drafts (final)")
    ap.add_argument("--make-test-base", action="store_true",
                    help="sandbox only: publish a stand-in 1.1.0 pair to version from")
    ap.add_argument("--resave", type=int, nargs="+", metavar="RECORD",
                    help="publish an edit of these records (adding a missing creator "
                         "ORCID), so their DOIs reach ORCID through DataCite's auto-update")
    ap.add_argument("--orcid-prefer", action="store_true",
                    help="after --publish, make the two new works the ORCID preferred "
                         "source (needs the session cookie file; see orcid_prefer.py)")
    ap.add_argument("--orcid-wait", type=float, default=600.0, metavar="SECONDS",
                    help="with --orcid-prefer, wait this long for DataCite's "
                         "auto-update to reach ORCID (default: 600)")
    args = ap.parse_args()

    if args.orcid_prefer and not args.publish:
        raise SystemExit("zenodo_deposit.py: --orcid-prefer needs --publish")
    if args.orcid_prefer and args.host != "zenodo":
        raise SystemExit("zenodo_deposit.py: --orcid-prefer is for the production host")

    token = os.environ.get("ZENODO_TOKEN")
    if not token:
        raise SystemExit("zenodo_deposit.py: set ZENODO_TOKEN")
    z = Zenodo(HOSTS[args.host], token)

    if args.make_test_base:
        if args.host != "sandbox":
            raise SystemExit("zenodo_deposit.py: --make-test-base is for the sandbox only")
        make_test_base(z)
        return 0
    if args.resave:
        resave(z, args.resave)
        return 0

    records = {"source": args.source_record, "docs": args.docs_record}
    if args.host == "zenodo":
        records = {k: v or PRODUCTION_RECORDS[k] for k, v in records.items()}
    elif None in records.values():
        raise SystemExit("zenodo_deposit.py: a sandbox run needs --source-record and "
                         "--docs-record (see --make-test-base)")

    tag = args.tag or newest_tag()
    version, count, cff = release_info(tag, args.citation)
    print(f"==> {tag}: version {version}, build count {count}, host {args.host}")

    # A line that already has this version is done; making another would give
    # the same release a second DOI.
    for kind, rid in records.items():
        latest = latest_published(z, rid)
        if latest["metadata"].get("version") == version:
            raise SystemExit(f"zenodo_deposit.py: the {kind} line already has {version}: "
                             f"{latest['doi']}")
        if latest["id"] != rid:
            raise SystemExit(f"zenodo_deposit.py: record {rid} is not the newest {kind} "
                             f"version ({latest['id']} is)")

    with tempfile.TemporaryDirectory(prefix="bovnar-zenodo-") as work:
        files = {"source": source_files(tag, version, count, work),
                 "docs": docs_files(tag, work, args.pdf_dir)}

        drafts, dois = {}, {}
        for kind in ("source", "docs"):
            draft = open_draft(z, records[kind])
            drafts[kind], dois[kind] = reserve_doi(z, draft)
            print(f"==> {kind}: draft {draft['id']}, DOI {dois[kind]}")

        for kind, other in (("source", "docs"), ("docs", "source")):
            draft = drafts[kind]
            print(f"==> {kind}: files")
            replace_files(z, draft, files[kind])
            meta = metadata_for(kind, draft["metadata"], tag, version, cff,
                                dois[kind], dois[other])
            z.call("PUT", draft["links"]["self"], body={"metadata": meta})
            drafts[kind] = z.call("GET", draft["links"]["self"])

    published = {}
    for kind in ("source", "docs"):
        d = drafts[kind]
        if args.publish:
            p = z.call("POST", d["links"]["publish"])
            published[kind] = p.get("doi") or dois[kind]
            print(f"==> {kind}: published {p.get('doi')}  "
                  f"{p['links'].get('html') or p['links'].get('record_html', '')}")
            # The cross-links were written with the reserved DOI. If the record
            # came out under another, they point at nothing -- say so. (The
            # sandbox does this: it reserves 10.5281/... and mints 10.5072/...)
            if p.get("doi") != dois[kind]:
                print(f"    WARNING: reserved {dois[kind]} but published as "
                      f"{p.get('doi')}; the other record's link to it is wrong")
            time.sleep(1)
        else:
            print(f"==> {kind}: draft ready, DOI {dois[kind]} on publish  "
                  f"{d['links'].get('html', '')}")
    if not args.publish:
        print("Review the drafts, then run again with --publish.")
        return 0
    if args.orcid_prefer:
        print("==> ORCID: making the new works the preferred source")
        cmd = [sys.executable, os.path.join(ROOT, "orcid_prefer.py"),
               "--tag", tag,
               "--source-doi", published.get("source", dois["source"]),
               "--docs-doi", published.get("docs", dois["docs"]),
               "--wait", str(args.orcid_wait)]
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
