"""readme_txt.py README.md OUT.txt -- the Share package's README.txt from the public README.md (step 110, Part 5, notes
18.113): one text for both. Plain text for Notepad: headings underlined, tables as aligned columns, links as
"text (address)", backticks and bold marks dropped, HTML comments (the owner's notes) left out, paragraphs wrapped at
100 columns, CRLF line ends. The "Where to report problems" line's address becomes make_package.ps1's @ISSUES_URL@
token (filled from twoc/ISSUES_URL.txt, the one place it is written) and the "For developers" section is left out (the
package has no source tree)."""
import re, sys, textwrap

src, out = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
text = re.sub(r"<!--.*?-->\s*", "", text, flags=re.S)
text, n = re.subn(r"(?m)^(Where to report problems: \*\*)[^*\n]+(\*\*)", r"\1@ISSUES_URL@\2", text)
assert n == 1, "README.md has no \"Where to report problems: **...**\" line"
W = 100


def inline(s):
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1 (\2)", s)
    s = s.replace("**", "").replace("`", "")
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"\1", s)
    return s


lines = text.splitlines()
res = []
i = 0
skip = False
while i < len(lines):
    l = lines[i]
    m = re.match(r"^(#{1,3})\s+(.*)$", l)
    if m:
        title = inline(m.group(2))
        skip = title.lower() == "for developers"
        if not skip:
            if res and res[-1] != "":
                res.append("")
            if m.group(1) == "#":
                res += [title, "=" * len(title)]
            else:
                res += ["", title, "-" * len(title)]
        i += 1
        continue
    if skip:
        i += 1
        continue
    if l.startswith("|"):                      # a table: aligned columns, the separator row dropped
        rows = []
        while i < len(lines) and lines[i].startswith("|"):
            cells = [inline(c.strip()) for c in lines[i].strip().strip("|").split("|")]
            if not all(re.fullmatch(r":?-{3,}:?", c) or c == "" for c in cells):
                rows.append(cells)
            i += 1
        rows = [r for r in rows if any(r)]
        if rows:
            wid = [max(len(r[k]) for r in rows if k < len(r)) for k in range(max(len(r) for r in rows))]
            for r in rows:
                res.append("  " + "  ".join(c.ljust(wid[k]) for k, c in enumerate(r)).rstrip())
        continue
    m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", l)
    if m:                                       # a list item and its continuation lines
        body = [m.group(3)]
        i += 1
        while i < len(lines) and lines[i].startswith("  ") and not re.match(r"^\s*([-*]|\d+\.)\s", lines[i]):
            body.append(lines[i].strip())
            i += 1
        mark = "-" if m.group(2) in "-*" else m.group(2)
        ind = len(m.group(1))
        res += textwrap.wrap(inline(" ".join(body)), W, initial_indent=" " * ind + mark + " ",
                             subsequent_indent=" " * (ind + len(mark) + 1))
        continue
    if l.startswith("    "):                    # a code line: as it is
        res.append(l)
        i += 1
        continue
    if not l.strip():
        if res and res[-1] != "":
            res.append("")
        i += 1
        continue
    para = [l.strip()]                          # a paragraph
    i += 1
    while i < len(lines) and lines[i].strip() and not re.match(r"^(#|\||\s*([-*]|\d+\.)\s|    )", lines[i]):
        para.append(lines[i].strip())
        i += 1
    res += textwrap.wrap(inline(" ".join(para)), W)
while res and res[-1] == "":
    res.pop()
open(out, "w", encoding="utf-8", newline="\r\n").write("\n".join(res) + "\n")
print("README.txt: %d lines from %s" % (len(res), src))
