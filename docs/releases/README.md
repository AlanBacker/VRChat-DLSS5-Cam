# Release notes

One file per release and language, read by the release workflow and by the program:

| File | Used for |
|---|---|
| `v2.0.2.md` | The English notes. The release workflow puts them on the release page when the tag carries this file (a tag without it gets a generic text and GitHub's generated notes). |
| `v2.0.2.zh-CN.md`, `v2.0.2.ja.md`, `v2.0.2.ko.md` | The same notes in 简体中文, 日本語 and 한국어. The program's update window shows the notes in the interface's language; it reads these files from the repository at the release's tag (directly or through the GitHub mirror site in use) and falls back to the English page text for a language without a file. |

The files are plain Markdown. The program shows them as text: headings, bold and code markers are taken off, list items become bullets, and tables (the list of files at the end) are left out, so a release's text should carry its substance in paragraphs and lists. Commit the four files before the tag is created, so the tag carries them.
