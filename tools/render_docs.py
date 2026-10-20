#!/usr/bin/env python3
"""Renders README.md / docs/ARCHITECTURE.md / docs/BENUTZERANLEITUNG.md into
standalone styled HTML pages for the GitHub Pages site (see
tools/web-home/ for the landing page and .github/workflows/build.yml's
package-pages job for how this gets wired into the deploy). Not part of a
normal firmware build - only runs in CI (or manually) to produce the docs/
subdirectory of the published site.

Requires `pip install markdown`.
"""
import re
import sys
from pathlib import Path

import markdown

REPO = "https://github.com/dirkoberhaus/WiiBalanceBoardGate"

# Markdown links are written for how the docs read on GitHub (repo-root-
# relative). The rendered site has a different, flatter layout (see
# tools/web-home/ and the docs/*.html pages this script writes), so known
# cross-doc links get rewritten to match; anything not listed here (http(s)
# links, in-page #anchors) is left as-is.
LINK_REWRITES = {
    "README.md": "readme.html",
    "../README.md": "readme.html",
    "ARCHITECTURE.md": "architecture.html",
    "docs/ARCHITECTURE.md": "architecture.html",
    "BENUTZERANLEITUNG.md": "benutzeranleitung.html",
    "docs/BENUTZERANLEITUNG.md": "benutzeranleitung.html",
    "../tools/web-flash/": "../flash/",
    "tools/web-flash/": "../flash/",
    ".github/workflows/build.yml": f"{REPO}/blob/main/.github/workflows/build.yml",
    "LICENSE": f"{REPO}/blob/main/LICENSE",
    "NOTICE": f"{REPO}/blob/main/NOTICE",
}

PAGE_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} - WiiBalanceBoardGate</title>
<script src="https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js"></script>
<style>
  :root {{ color-scheme: dark; }}
  body {{
    margin: 0; padding: 24px 20px 60px; box-sizing: border-box;
    background: #14161a; color: #e8e8ea;
    font-family: -apple-system, system-ui, sans-serif;
    line-height: 1.6;
    max-width: 820px; margin-left: auto; margin-right: auto;
  }}
  nav {{ margin-bottom: 24px; font-size: 0.9rem; }}
  nav a {{ color: #9aa0a8; text-decoration: none; }}
  nav a:hover {{ color: #e8e8ea; }}
  h1, h2, h3, h4 {{ font-weight: 600; }}
  h1 {{ font-size: 1.6rem; border-bottom: 1px solid #3a3f47; padding-bottom: 10px; }}
  h2 {{ font-size: 1.25rem; margin-top: 2.2em; border-bottom: 1px solid #2a2f38; padding-bottom: 6px; }}
  h3 {{ font-size: 1.05rem; margin-top: 1.8em; color: #c4c8ce; }}
  h4 {{ font-size: 0.95rem; margin-top: 1.5em; color: #c4c8ce; }}
  a {{ color: #4ade80; }}
  code {{
    background: #1e2128; padding: 2px 5px; border-radius: 4px;
    font-size: 0.88em; font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  }}
  pre {{
    background: #1e2128; border: 1px solid #3a3f47; border-radius: 10px;
    padding: 14px; overflow-x: auto;
  }}
  pre code {{ background: none; padding: 0; }}
  pre.mermaid {{ background: #ffffff; border-color: #ffffff; text-align: center; padding: 20px; }}
  table {{ border-collapse: collapse; width: 100%; margin: 1em 0; font-size: 0.92rem; }}
  th, td {{ border: 1px solid #3a3f47; padding: 8px 10px; text-align: left; }}
  th {{ background: #1e2128; }}
  blockquote {{
    border-left: 3px solid #3a3f47; margin: 1em 0; padding: 0.2em 1em;
    color: #9aa0a8;
  }}
</style>
</head>
<body>
<nav><a href="../">&larr; WiiBalanceBoardGate</a></nav>
{body}
<script>mermaid.initialize({{ startOnLoad: true, theme: "dark" }});</script>
</body>
</html>
"""


def rewrite_links(html: str) -> str:
    def repl(m: re.Match) -> str:
        href = m.group(1)
        base, _, fragment = href.partition("#")
        if base in LINK_REWRITES:
            new = LINK_REWRITES[base]
            return f']({new}#{fragment})' if fragment else f']({new})'
        return m.group(0)

    return re.sub(r"\]\(([^)]+)\)", repl, html)


def render(md_path: Path, out_path: Path, title: str) -> None:
    text = md_path.read_text(encoding="utf-8")
    # Must happen on the raw Markdown, before conversion - by the time
    # markdown.markdown() has run, `](url)` syntax is already gone, replaced
    # with <a href="url">.
    text = rewrite_links(text)
    body = markdown.markdown(
        text, extensions=["fenced_code", "tables", "toc", "sane_lists"]
    )
    # Fenced ```mermaid blocks render as <pre><code class="language-mermaid">
    # (already correctly HTML-escaped by the fenced_code extension) -
    # mermaid.js's default selector is `pre.mermaid`, so just restructure
    # the tags; .textContent decodes the entities back for mermaid.js
    # regardless of how they're escaped in the markup.
    body = re.sub(
        r'<pre><code class="language-mermaid">(.*?)</code></pre>',
        r'<pre class="mermaid">\1</pre>',
        body,
        flags=re.DOTALL,
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(PAGE_TEMPLATE.format(title=title, body=body), encoding="utf-8")
    print(f"Wrote {out_path}")


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else repo_root / "site" / "docs"

    render(repo_root / "README.md", out_dir / "readme.html", "README")
    render(
        repo_root / "docs" / "ARCHITECTURE.md",
        out_dir / "architecture.html",
        "Architecture",
    )
    render(
        repo_root / "docs" / "BENUTZERANLEITUNG.md",
        out_dir / "benutzeranleitung.html",
        "Benutzeranleitung",
    )


if __name__ == "__main__":
    main()
