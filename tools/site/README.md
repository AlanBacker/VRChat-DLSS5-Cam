# Documentation site

The source of the documentation site at https://alanbacker.github.io/VRChat-DLSS5-Cam/. The build writes plain HTML,
CSS and JavaScript into `site/` at the repository root; GitHub Pages publishes that folder
(`.github/workflows/pages.yml`). The pages make no request outside the site (AI Q&A aside, when it is turned on) and
also work when opened straight from the disk (`site/index.html`).

## Build

```
python3 tools/site/build.py            # build every language into site/
python3 tools/site/build.py --check    # build, then check every internal link and picture
```

Python 3.8 or newer with Pillow (it makes the WebP copies of the screenshots). Nothing else, no npm. The build
rewrites its files in `site/` each time and removes pages that are no longer listed, so edit `tools/site/`, never
`site/`. It ends with a list of warnings: missing pages or strings, missing pictures, PNG sources that would be
published unpainted, and with `--check` any link or picture that does not resolve.

## Folders

| Path | What it holds |
|---|---|
| `site.json` | Site name, address, base path, download file names, the four languages and the page list with its two sections (Guide, Reference) |
| `content/<lang>/<page>.md` | The pages, one file per page and language (`en`, `zh`, `ja`, `ko`), same file names in every language |
| `strings/<lang>.json` | Every word of the frame: navigation, buttons, callout titles, diagrams, search, the 404 page |
| `images.json` | The screenshots and how each is cropped, painted over and shown |
| `shots/` | The screenshots' PNG sources (not published; see Screenshots) |
| `templates/layout.html` | The page frame |
| `assets/` | `site.css`, `site.js`, the logo files; copied into `site/assets/`. `ask.css` and `ask.js` are added to `site.css` and `site.js` only when AI Q&A is on |
| `icons/` | Lucide icons (ISC licence in `icons/LICENSE`), inlined into the pages as SVG |
| `partials/` | The two sphere drawings used by the comparison illustration (the home page shows `assets/logo.svg`) |
| `mintlify.py` | Writes `mintlify/`, the copy of the pages that Mintlify's AI Q&A answers from (see The Mintlify copy) |
| `logo.py` | Redraws `assets/logo.svg`, `assets/mark.svg`, the PNG icons and `partials/` from `tools/make_app_icon.py`; its output is committed, run it only when the program icon changes (needs pycairo) |

The changelog page is built from `docs/releases/vX.Y.Z[.zh-CN|.ja|.ko].md`: each version's first paragraph and the
bold first words of each item.

## Translating

1. Open `content/<lang>/<page>.md`. An untranslated page is a copy of the English one with `status: to-be-translated`
   in its front matter; the build shows it with a note that it is in English.
2. Translate the front matter values and the text. Keep the file name, the `{#id}` anchors, the shortcodes, the
   picture names and the links as they are, so links between pages and languages keep working.
3. When the page is done, write `status: translated` (a page without a `status:` line counts as untranslated).
4. Translate `strings/<lang>.json` the same way and set `"_status": "translated"`. Only then does the one-time bar
   at the top of the pages offer that language to readers whose browser prefers it, and the 404 page show its
   message in that language.

Front matter, at the top of each page:

```
---
title: First picture
nav: First picture
description: Open a VRChat screenshot, look at the result and save it.
status: to-be-translated
---
```

`title` is the heading and the browser tab, `nav` the shorter name in the sidebar, `description` the line under the
heading and the search engines' summary.

Write control names exactly as the app shows them in that language (the app's own strings are in `src/core/I18n.h`).
Each language has its own screenshots where the interface's words show: `shots/<lang>/<name>.png` (see Screenshots). A
picture a language does not have shows the English copy.

## The Markdown used here

Blocks:

- `## Heading`, `### Heading`, `#### Heading`, optionally ending in `{#anchor}`; headings get anchors and appear in
  the page's contents list and in the search.
- Paragraphs, `- ` lists, and `1. ` lists, which are drawn as numbered steps. Indent a line to continue an item.
- `=> What you should see` after a step: drawn as a check line.
- `> [!TIP]`, `> [!WARNING]`, `> [!NOTE]` callouts, followed by `> ` lines.
- Tables. A row that ends in `{#anchor}` can be linked to and is found by the search (used for the settings).
- ```` ``` ```` code blocks (` ```json ` for JSON); they get a copy button.
- `![Alt text](picture-name "Caption")`: a screenshot from `images.json`. The alt text says what the picture shows.
- Shortcodes, alone on a line: `{{diagram:live}}`, `{{diagram:pipeline}}`, `{{diagram:update}}`, `{{wipe}}` (the
  before/after illustration), `{{downloads}}` (the three download cards), `{{tree}}` (the program folder),
  `{{pagelist}}` and `{{changelog}}`. Their words come from `strings/<lang>.json`.
- `<!-- comments -->` are dropped.

Inline: `**bold**`, `*italic*`, `` `code` ``, `[text](page.html#anchor)`, `[[Control name]]` (a name from the
app's interface), `((Ctrl+Alt+P))` (keys), `{adv}` (the Advanced badge for controls shown only with Advanced on).

## Screenshots

The pictures are `<name>.png` files: the interface in the dark theme, whole windows at 1600 x 1000 or sidebar
sections at their own size. The English sources are `tools/site/shots/<name>.png`, the Chinese, Japanese and Korean
ones `tools/site/shots/<lang>/<name>.png` (`zh`, `ja`, `ko`), taken with the interface in that language (`--lang`)
and the same window size, state and crop as the English picture. `shots/` is not committed (`.gitignore`): the
sources carry private pixels. The build also reads English sources from `site/assets/img/`, but whatever lies there
is published as it is. The build never changes the PNG files; it writes WebP copies (and smaller ones for phones)
into `site/assets/img/gen/` and `gen/<lang>/`, which the pages use and which are committed. A clone without the
sources keeps those copies as they are, so it builds the same pages; a new or changed screenshot needs its source in
`shots/`. A language without its own copy of a picture shows the English one.
In `images.json`:

- `cover`: rectangles `[left, top, right, bottom]` in source pixels, painted with the colour just right of each
  one, or at `[.., x, y]` when two more numbers are given. Used to keep a graphics card's model, a user folder or a
  test version out of the published copies.
- `crop`: `[left, top, right, bottom]` cut out of the source after painting.
- `width`: the largest width of the published copy.
- `look`: `window` (a whole window, as wide as the text) or `panel` (part of a window, at most `show` CSS pixels
  wide).
- `langs`: `{"zh": {...}}` with a language's own `cover`, `crop` or `width`, for a picture whose words sit at other
  places in that language; the English values apply otherwise.

A picture that is missing is left out of the pages and named in the build's warnings. Pictures in the pages: no
name plates, user IDs, error messages or private paths, and the same views in all four languages.

## The Mintlify copy

`mintlify/` at the repository root holds the same pages as `.mdx` files with a `docs.json`, written by
`mintlify.py` on every build. Mintlify reads that folder from the default branch so that its AI Q&A answers
questions from these pages; the site in `site/` stays the documentation readers use (the copy tells search engines
not to list it). Never edit `mintlify/` by hand.

- In the Mintlify dashboard, Git settings: turn on **docs.json is in a subdirectory** and enter `/mintlify`.
- The folders keep the site's language codes (`en`, `zh`, `ja`, `ko`); `docs.json` names them with Mintlify's
  codes (`en`, `cn`, `jp`, `ko`).
- Pictures are not copied: the pages show the WebP copies published on the site, with the alt texts from
  `strings/<lang>.json`. The draggable comparison links to the site. The `{#anchor}` ids are dropped, so Mintlify
  makes its own from the headings.
- To check the copy: `npm install mint` in a scratch folder, then in `mintlify/` run `mint validate` and
  `mint broken-links` (or `mint dev` for a preview).

## AI Q&A

`askWidget` in `site.json` holds the public ID of a Mintlify widget (not a secret: every page carries it). Left
empty, the pages carry no trace of the feature: the build output is the same as without it. With an ID, the build:

- adds an **Ask AI** button beside the search box, and an "Ask AI about …" row at the end of the search results;
- adds `assets/ask.css` and `assets/ask.js` to the end of `site.css` and `site.js` (no extra file to fetch);
- writes the ID, Mintlify's language code, the labels and the starter questions (`ask` in `strings/<lang>.json`, at
  most three questions) into each page's `data-site`;
- keeps what stands between `<!-- if askWidget -->` and `<!-- else -->` in a page, and drops the `else` part.
  Without the ID it is the other way round. Each line stands alone, and `<!-- else -->` may be left out:

  ```
  <!-- if askWidget -->
  Text for pages with AI Q&A.
  <!-- else -->
  Text for pages without it.
  <!-- endif -->
  ```

How it behaves:

- Nothing from Mintlify loads with a page. Pointing at an entry point, or moving to it with the keyboard, fetches the
  widget's script once the page has finished opening. That draws nothing and sends nothing. A click starts the
  widget and opens its panel.
- The widget takes the site's accent colour, card radius, font, mark and theme, and follows a theme change.
- Mintlify draws its own button in the corner whenever the panel is closed, and its stylesheet resets its element
  with `all: initial !important`, which no page style can override. `ask.js` moves that element into a layer of
  the page (`.ask-layer`) that is there only while the panel is open: it fades in as the panel opens, fades out in
  0.08 s as the panel starts to slide away (the panel covers the corner button for its first 0.1 s), then leaves
  the page. The button is never seen; **Ask AI** in the top bar is the way back to the conversation. The widget is
  started open, so it is drawn open from the start. Mintlify's own controls in the panel (**Clear chat**, its error
  messages) stay in English: the widget has no labels for them.
- An answer's link to another page of the site puts that page in place of this one without a page load, so the
  panel and the conversation stay: the page's text fades out, the sidebar's highlight and the top bar's tab glide
  to the new page as after a followed link, and its text rises in. The address and the history follow, and the back
  and forward buttons swap the pages back to where the reader was. On a phone, where the panel covers the page, the
  panel closes first and **Ask AI** brings the conversation back. A link to a heading on the same page glides there;
  a page in another language opens in a new tab. `ask.js` does the swap; `site.js` sets up the new page's parts
  (copy buttons, contents, figures, pictures) on its `vdc:swap` event, and the layout's sidebar script glides the
  highlight on `vdc:moved`.
- Mintlify writes those links in its own form (`/zh/install`), usually with the site's base path before it
  (`/VRChat-DLSS5-Cam/zh/install`), and sometimes as a whole address (`https://alanbacker.github.io/…/install.html`);
  all of them count. The widget announces a click on its own-form links with an event, but a whole address is a
  plain new-tab link there, and its panel is a closed shadow root that hides the clicked link from the page. So
  `ask.js` keeps a reference to that root as the widget makes it (`attachShadow` is wrapped only while the widget
  starts, and only for its element) and answers a click on a link to a page here before the widget does. Links to
  other sites, and clicks that ask for a new tab (Ctrl, Shift, the middle button), open as usual.
- Once the widget has started, the page's own links to other pages of the same language (sidebar, tabs, text links,
  search results, the previous and next pages, the logo) are followed the same way, so moving through the guide never
  restarts the widget or loses the conversation, whether the panel is open or closed. A link to the page being read
  glides to its heading or to the top. Before the widget starts, links load pages as usual.
- Docked at the side, the panel would cover the right of the page on a 1080p screen. Where the window leaves the page
  at least 760 px beside the panel (a window of 1256 px or more), the page makes room instead: `ask.js` sets
  `ask-room` and `--ask-room` (the panel's width and margins, measured) on the root, and `ask.css` keeps the page
  and the top bar clear of the panel, with the top bar's and footer's backgrounds running on under it. The page is
  laid out for the width that remains: three columns from 1240 px (their gaps close from 48 to 32 px before the text
  column narrows, so a 1920 px window keeps the 800 px text column), two columns (`ask-r2`, the contents list moves
  into the text) from 1000 px, one column below that (`ask-r1`, the sidebar becomes the sheet behind the menu button).
  No media query sees that width, so `build.py` repeats each rule of `site.css`'s 1239 px and 999 px media blocks
  under `.ask-r2` and `.ask-r1` (`room_rules`); a rule added to those blocks gets its copy on the next build. When
  the text keeps its column width, the page slides over with the panel on the panel's 0.45 s curve, and back on the
  program's 0.28 s curve as it leaves. When it would be laid out again, it fades out (0.12 s), is laid out with the
  line being read kept in place, and fades back in (0.28 s) while the top bar slides. With reduced motion it switches
  at once. A narrower window, and a phone, keep the panel over the page.
- An address with `?ask` (for example `…/zh/?ask`) opens the panel as the page opens, then drops the parameter from
  the address. The program uses it where it cannot show the panel itself.
- The program has its own AI Q&A panel (**Ask AI** in its top bar, `src/core/AskPanel.cpp`) with the same widget. It
  serves its small page itself under the site's address (`…/app-ask/`, never fetched from the site), so the allowed
  origin covers it. It opens an answer's link to these pages in the interface's language, and it knows the pages by
  their slugs (`kPages`): a link to a slug missing there opens the start page. Add a new page's slug to `kPages` too.
- Mintlify checks each question with hCaptcha (its bot protection, set in the dashboard). A browser hCaptcha
  trusts passes unseen; others, such as a headless browser on a server, first get a picture puzzle above the page.

To turn it on:

1. Mintlify needs a Pro or Enterprise plan for the widget (its OSS program gives eligible open-source projects
   Pro for free: https://mintlify.typeform.com/oss-program). Connect the project to `mintlify/` first (see The
   Mintlify copy), because the widget answers from that content.
2. In the dashboard, open the **Widget** page and turn the widget on.
3. Add `https://alanbacker.github.io` as an allowed origin. To try it locally, serve `site/` (for example
   `python3 -m http.server` in that folder, which gives `http://localhost:8000`) and add that address too. Pages
   opened straight from the disk have no origin the widget can allow.
4. Copy the widget ID into `"askWidget"` in `site.json`, then run the build.

The `ask` strings in `strings/<lang>.json` and the privacy page's `<!-- if askWidget -->` block exist in all four
languages; a new language needs both.

## Base path

The site is served from `/VRChat-DLSS5-Cam/`. Every link in the pages is relative; only the 404 page, which GitHub
Pages serves at any missing address, uses `basePath` from `site.json`. Change it there if the repository is renamed.
