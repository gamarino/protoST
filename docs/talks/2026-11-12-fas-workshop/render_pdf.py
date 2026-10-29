#!/usr/bin/env python3
"""Print deck.html to slides/protoST-por-dentro-FAST-2026-11-12.pdf.

deck.html is the source: one <section> per slide on a 1920x1080 canvas, every
style inline, speaker notes in <aside>. A browser needs a few rules to print it
as one slide per page (zero margins, notes hidden, table rules, the x-shape
arrows), so this script adds them to a temporary copy and prints that copy
with a headless Chromium (Chrome, Chromium or Edge).

Usage: python3 render_pdf.py [path/to/chrome]
"""
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "slides" / "protoST-por-dentro-FAST-2026-11-12.pdf"

PRINT_CSS = """<style>
@page { size: 1920px 1080px; margin: 0; }
html, body { margin: 0; padding: 0; }
* { box-sizing: border-box; margin: 0; }
section { width: 1920px; height: 1080px; position: relative; overflow: hidden;
          page-break-after: always; break-after: page; }
aside { display: none; }
h1, h2, h3 { font-weight: 600; }
p { font-size: 32px; line-height: 1.4; }
ul, ol { font-size: 32px; line-height: 1.4; padding-left: 1.3em; }
li + li { margin-top: 0.2em; }
table { border-collapse: collapse; width: 100%; }
th, td { padding: 0.35em 0.6em; text-align: left; vertical-align: top;
         border-bottom: 1px solid rgba(127,127,127,0.35); }
th { font-weight: 600; }
[style*="JetBrains"] { font-variant-ligatures: none; }
x-shape { display: block; flex: none; }
x-shape[kind="arrow-right"] {
  clip-path: polygon(0 30%, 60% 30%, 60% 0, 100% 50%, 60% 100%, 60% 70%, 0 70%); }
</style>
"""

CANDIDATES = ["google-chrome", "chromium", "chromium-browser", "msedge",
              r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe",
              "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"]


def find_browser():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for c in CANDIDATES:
        found = shutil.which(c) or (c if Path(c).exists() else None)
        if found:
            return found
    sys.exit("no Chromium found; pass its path as the first argument")


def wait_for_pdf(timeout=120):
    # Edge on Windows hands the job to a browser process and returns before
    # the PDF is written: wait until the file exists and stops growing.
    deadline = time.time() + timeout
    last = -1
    while time.time() < deadline:
        size = OUT.stat().st_size if OUT.exists() else -1
        if size > 0 and size == last:
            return
        last = size
        time.sleep(1)
    sys.exit(f"no PDF written to {OUT}")


def main():
    deck = (HERE / "deck.html").read_text(encoding="utf-8")
    tmp = tempfile.mkdtemp()
    try:
        page = Path(tmp) / "print.html"
        page.write_text(deck.replace("</head>", PRINT_CSS + "</head>", 1), encoding="utf-8")
        OUT.parent.mkdir(exist_ok=True)
        OUT.unlink(missing_ok=True)
        subprocess.run([find_browser(), "--headless=new", "--disable-gpu",
                        "--no-pdf-header-footer", f"--user-data-dir={tmp}/profile",
                        "--virtual-time-budget=20000", f"--print-to-pdf={OUT}",
                        page.as_uri()], check=True)
        wait_for_pdf()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(OUT)


if __name__ == "__main__":
    main()
