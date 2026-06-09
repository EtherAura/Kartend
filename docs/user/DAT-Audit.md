# DAT Audit

The **DAT Audit** window checks the files in a folder against one or more **DAT
catalogues** — community-maintained lists that map a file's content hash to its
canonical name — and reports what you have, what's misnamed, what's unrecognised,
and what's missing. It's the verification companion to the
[Scraper](Scraper.md): the scraper *fetches metadata*, the audit *verifies your
files*.

DAT catalogues exist for many curated media sets (reference archives, audio
collections, video libraries, and more). If you have a `.dat` / `.xml` catalogue
for your collection, the audit tells you at a glance how complete and how
correctly-named it is.

Open it from **File → DAT Audit…**.

## Running an audit

1. **Add DAT files** — click *Add DAT…* and pick one or more `.dat` / `.xml`
   catalogues.
2. **Add scan folders** — click *Add folder…* and pick the folder(s) holding the
   files to check.
3. Click **Run audit**. Hashing runs in the background; click **Cancel** to stop.

When it finishes, the summary line shows the totals and the table lists every
file and every catalogue entry with a status:

| Status | What it means | What to do |
|--------|---------------|------------|
| **Have** | Correct file, correct name | Nothing |
| **Wrong name** | Right content, wrong filename | Rename to the canonical name |
| **Wrong content** | Right name, wrong/old content | Re-acquire a good copy |
| **Duplicate** | A second copy of something you already have | Optional cleanup |
| **Unknown** | Not in any catalogue | Identify or set aside |
| **Missing** | In the catalogue, not in your folder | Acquire it |

Use the **Show** filter to focus on one status (e.g. just *Missing*, or just
*Wrong name*).

## Exporting results

- **Export CSV** — the full table as a spreadsheet, one row per entry with a
  Status column you can sort/filter.
- **Export fixdat** — a DAT file containing *only the missing entries*. Hand it
  to any DAT tool to fetch exactly the gaps.
- **Export miss list** — a plain-text list of the missing titles.

## Notes

- Auditing matches on **content hash**, so a correctly-downloaded but
  wrongly-named file is still recognised (and flagged *Wrong name*).
- A first audit hashes every file, which can take a while for large media; the
  status line shows progress.
- The DAT files themselves are never reported, even if they sit inside a scan
  folder.
