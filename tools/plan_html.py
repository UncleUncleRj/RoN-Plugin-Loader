"""Turn one implementation plan (Markdown) into a single self-contained HTML file for review.

Usage: python tools/plan_html.py docs/plans/<plan>.md <out.html>

Handles the Markdown the plans use: headings, paragraphs, nested bullet and numbered lists with
continuation lines, "- [ ]" checkboxes, fenced code, pipe tables, block quotes, rules, `code`,
**bold**. Every "### Task N" becomes a section with a progress counter; checkbox state is kept
in the browser's localStorage, keyed by the plan's file name. Standard library only.
"""
import html
import re
import sys
from pathlib import Path

LIST_RE = re.compile(r"^(\s*)([-*]|\d+\.)\s+(.*)$")


def inline(text):
    parts = re.split(r"(`[^`]*`)", text)
    out = []
    for part in parts:
        if len(part) >= 2 and part.startswith("`") and part.endswith("`"):
            out.append("<code>" + html.escape(part[1:-1], quote=False) + "</code>")
        else:
            t = html.escape(part, quote=False)
            t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
            out.append(t)
    return "".join(out)


def slug(text):
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s or "section"


class Ctx:
    def __init__(self, plan_key):
        self.plan_key = plan_key
        self.box = 0
        self.task = None


def render_item_text(paras, ctx):
    first = paras[0] if paras else ""
    rest = paras[1:]
    m = re.match(r"^\[( |x)\]\s+(.*)$", first, re.S)
    if m:
        ctx.box += 1
        bid = f"{ctx.task or 'misc'}-{ctx.box}"
        checked = " data-default-checked" if m.group(1) == "x" else ""
        head = (f'<label class="step"><input type="checkbox" data-box="{bid}"{checked}>'
                f'<span>{inline(m.group(2))}</span></label>')
    else:
        head = inline(first)
    return head + "".join(f"<p>{inline(p)}</p>" for p in rest)


def parse_list(lines, i, base_indent, ctx):
    """Parse a list starting at lines[i] whose markers sit at base_indent. Returns (html, i)."""
    m = LIST_RE.match(lines[i])
    ordered = m.group(2)[0].isdigit()
    tag = "ol" if ordered else "ul"
    has_box = False
    items = []
    while i < len(lines):
        line = lines[i]
        m = LIST_RE.match(line)
        if not m or len(m.group(1)) != base_indent or (m.group(2)[0].isdigit()) != ordered:
            break
        paras = [m.group(3)]
        is_box = bool(re.match(r"^\[( |x)\]\s", m.group(3)))
        if items and is_box != has_box:
            break
        has_box = is_box
        children = []
        i += 1
        blank = False
        while i < len(lines):
            line = lines[i]
            if not line.strip():
                blank = True
                i += 1
                continue
            indent = len(line) - len(line.lstrip(" "))
            if line.lstrip().startswith("```") and indent > base_indent:
                code_html, i = parse_code(lines, i, ctx)
                children.append(code_html)
                blank = False
                continue
            lm = LIST_RE.match(line)
            if lm and len(lm.group(1)) > base_indent:
                sub, i = parse_list(lines, i, len(lm.group(1)), ctx)
                children.append(sub)
                blank = False
                continue
            if indent > base_indent and not lm:
                text = line.strip()
                if blank or children:
                    if children:
                        children.append(f"<p>{inline(text)}</p>")
                    else:
                        paras.append(text)
                else:
                    paras[-1] += " " + text
                blank = False
                i += 1
                continue
            break
        items.append("<li>" + render_item_text(paras, ctx) + "".join(children) + "</li>")
        if i < len(lines) and not lines[i].strip():
            i += 1
        if i < len(lines):
            nm = LIST_RE.match(lines[i])
            if not nm or len(nm.group(1)) != base_indent:
                break
    cls = ' class="steps"' if has_box else ""
    return f"<{tag}{cls}>" + "".join(items) + f"</{tag}>", i


def parse_code(lines, i, ctx):
    opener = lines[i].strip()
    lang = opener[3:].strip()
    indent = len(lines[i]) - len(lines[i].lstrip(" "))
    i += 1
    body = []
    while i < len(lines) and not lines[i].strip().startswith("```"):
        body.append(lines[i][indent:] if lines[i][:indent].strip() == "" else lines[i])
        i += 1
    i += 1
    cls = f' class="lang-{html.escape(lang)}"' if lang else ""
    return f"<pre><code{cls}>" + html.escape("\n".join(body), quote=False) + "</code></pre>", i


def parse_table(lines, i):
    rows = []
    while i < len(lines) and lines[i].lstrip().startswith("|"):
        cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
        rows.append(cells)
        i += 1
    head, body = rows[0], [r for r in rows[2:]]
    t = ['<div class="tbl"><table><thead><tr>']
    t += [f"<th>{inline(c)}</th>" for c in head]
    t.append("</tr></thead><tbody>")
    for r in body:
        t.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
    t.append("</tbody></table></div>")
    return "".join(t), i


def convert(md, plan_key):
    lines = md.replace("\r\n", "\n").split("\n")
    ctx = Ctx(plan_key)
    out, toc, tasks = [], [], []
    title = ""
    i = 0
    open_section = None
    while i < len(lines):
        line = lines[i]
        s = line.strip()
        if not s:
            i += 1
            continue
        if s.startswith("```"):
            code_html, i = parse_code(lines, i, ctx)
            out.append(code_html)
            continue
        h = re.match(r"^(#{1,3})\s+(.*)$", line)
        if h:
            level, text = len(h.group(1)), h.group(2)
            if level == 1:
                title = text
                out.append(f'<header class="hero"><h1>{inline(text)}</h1>')
                open_section = "hero"
            else:
                if open_section:
                    out.append("</header>" if open_section == "hero" else "</section>")
                tm = re.match(r"^Task (\d+):\s*(.*)$", text) if level == 3 else None
                if tm:
                    n = tm.group(1)
                    ctx.task, ctx.box = f"task-{n}", 0
                    tasks.append((n, tm.group(2)))
                    toc.append(("task", f"task-{n}", n, tm.group(2)))
                    out.append(f'<section class="task" id="task-{n}"><h3><span class="num"><span class="lbl">Task</span>{n}</span>'
                               f'<span class="ttl">{inline(tm.group(2))}</span>'
                               f'<span class="prog" data-prog="task-{n}"></span></h3>')
                else:
                    sid = slug(text)
                    ctx.task, ctx.box = sid, 0
                    toc.append(("sec", sid, None, text))
                    out.append(f'<section class="sec" id="{sid}"><h{level}>{inline(text)}</h{level}>')
                open_section = "section"
            i += 1
            continue
        if s == "---":
            out.append("<hr>")
            i += 1
            continue
        if s.startswith(">"):
            quote = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                quote.append(lines[i].strip()[1:].strip())
                i += 1
            out.append(f'<blockquote><p>{inline(" ".join(quote))}</p></blockquote>')
            continue
        if s.startswith("|"):
            t, i = parse_table(lines, i)
            out.append(t)
            continue
        lm = LIST_RE.match(line)
        if lm:
            l_html, i = parse_list(lines, i, len(lm.group(1)), ctx)
            out.append(l_html)
            continue
        para = [s]
        i += 1
        while i < len(lines) and lines[i].strip() and not LIST_RE.match(lines[i]) \
                and not re.match(r"^(#{1,3})\s", lines[i]) and not lines[i].strip().startswith(("```", "|", ">")) \
                and lines[i].strip() != "---":
            para.append(lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(para))}</p>")
    if open_section:
        out.append("</header>" if open_section == "hero" else "</section>")
    return title, toc, "\n".join(out)


CSS = r"""
:root{--paper:#F3F5F6;--ink:#1C2230;--muted:#556071;--rule:#D2D8DE;--bronze:#86642A;--bronze-soft:#E6DCC6;
--code:#E7EBEE;--done:#3C7550;--sans:"Segoe UI",system-ui,-apple-system,sans-serif;
--serif:"Palatino Linotype","Book Antiqua",Palatino,Georgia,serif;--mono:"Cascadia Mono",Consolas,"Courier New",monospace}
@media (prefers-color-scheme:dark){:root{--paper:#141820;--ink:#E2E5EA;--muted:#99A3B0;--rule:#2A313C;
--bronze:#D2A857;--bronze-soft:#3A3122;--code:#1C222B;--done:#7DB98C}}
*{box-sizing:border-box}
html{scroll-padding-top:1rem}
body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.62 var(--sans);
display:grid;grid-template-columns:17rem minmax(0,46rem);gap:3.5rem;justify-content:center;padding:0 1.5rem}
nav{position:sticky;top:0;height:100vh;overflow:auto;padding:2.2rem 0 2rem}
nav .plan{font:600 1.05rem/1.3 var(--serif);margin:0 0 1.4rem;color:var(--ink)}
nav ol{list-style:none;margin:0;padding:0}
nav li a{display:grid;grid-template-columns:1.6rem 1fr auto;gap:.6rem;align-items:center;padding:.28rem 0;
color:var(--ink);text-decoration:none;font-size:.9rem;line-height:1.3}
nav li a:hover .t{text-decoration:underline}
nav .sec-link a{grid-template-columns:1fr;color:var(--muted);padding:.22rem 0 .22rem 2.2rem}
nav .route{position:relative;margin:.6rem 0}
nav .route::before{content:"";position:absolute;left:.75rem;top:.9rem;bottom:.9rem;width:2px;background:var(--bronze-soft)}
nav .node{position:relative;width:1.6rem;height:1.6rem;border-radius:50%;display:grid;place-items:center;
font:600 .78rem/1 var(--serif);color:var(--ink);
background:conic-gradient(var(--bronze) calc(var(--p,0)*1turn),var(--paper) 0);border:2px solid var(--bronze)}
nav .node b{width:1.05rem;height:1.05rem;border-radius:50%;background:var(--paper);display:grid;place-items:center;font-weight:600}
nav .done .node b{background:var(--bronze);color:var(--paper)}
nav .cnt{color:var(--muted);font-size:.78rem;font-variant-numeric:tabular-nums}
nav button{margin-top:1.6rem;font:inherit;font-size:.82rem;color:var(--muted);background:none;border:1px solid var(--rule);
border-radius:3px;padding:.3rem .7rem;cursor:pointer}
nav button:hover{color:var(--ink);border-color:var(--muted)}
main{padding:2.2rem 0 6rem;min-width:0}
.hero h1{font:600 2.45rem/1.12 var(--serif);margin:0 0 1.2rem;letter-spacing:-.01em}
.hero blockquote{margin:0 0 1.6rem;padding:.2rem 0 .2rem 1rem;border-left:3px solid var(--rule);color:var(--muted);font-size:.9rem}
.hero > p:first-of-type{font-size:1.16rem;line-height:1.55}
.hero p strong:first-child{font-family:var(--serif);font-size:1.02em;color:var(--bronze)}
h2{font:600 1.6rem/1.2 var(--serif);margin:3rem 0 1rem;padding-top:1.6rem;border-top:1px solid var(--rule)}
.sec h3{font:600 1.25rem/1.25 var(--serif);margin:2rem 0 .8rem}
.task{margin-top:2.6rem;padding-top:1.6rem;border-top:1px solid var(--rule)}
.task h3{display:grid;grid-template-columns:auto 1fr auto;gap:1rem;align-items:end;margin:0 0 1rem;
font:600 1.4rem/1.25 var(--serif)}
.task .num{font:600 2.6rem/1 var(--serif);color:var(--bronze);font-variant-numeric:lining-nums tabular-nums;min-width:2.2ch;display:grid;justify-items:start}
.task .num .lbl{font:400 .78rem/1.2 var(--serif);color:var(--muted);letter-spacing:.02em;margin-bottom:.15rem}
.task .ttl{padding-bottom:.28rem}
.task .prog{padding-bottom:.4rem;font:400 .82rem/1 var(--sans);color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap}
.task.complete .prog{color:var(--done)}
p{margin:.6rem 0}
ul,ol{padding-left:1.35rem;margin:.6rem 0}
li{margin:.3rem 0}
li > p{margin:.4rem 0}
ul.steps,ol.steps{list-style:none;padding-left:0}
.steps > li{margin:.55rem 0}
label.step{display:grid;grid-template-columns:1.4rem 1fr;gap:.55rem;align-items:start;cursor:pointer}
label.step input{margin:.28rem 0 0;width:1.05rem;height:1.05rem;accent-color:var(--bronze);cursor:pointer}
label.step input:checked + span{color:var(--muted)}
.steps > li > ul,.steps > li > ol,.steps > li > p,.steps > li > pre,.steps > li > .tbl{margin-left:1.95rem}
code{font:0.86em/1.4 var(--mono);background:var(--code);padding:.08em .32em;border-radius:3px;overflow-wrap:anywhere}
pre{background:var(--code);padding:.9rem 1rem;border-radius:4px;overflow:auto;line-height:1.45;font-size:.84rem}
pre code{background:none;padding:0;font-size:inherit;overflow-wrap:normal}
.tbl{overflow:auto;margin:1rem 0}
table{border-collapse:collapse;width:100%;font-size:.88rem}
th,td{text-align:left;vertical-align:top;padding:.45rem .6rem;border-bottom:1px solid var(--rule)}
th{font-weight:600;border-bottom:2px solid var(--bronze-soft)}
hr{border:0;margin:0}
strong{font-weight:600}
a:focus-visible,button:focus-visible,input:focus-visible{outline:2px solid var(--bronze);outline-offset:2px}
footer{margin-top:4rem;color:var(--muted);font-size:.82rem;border-top:1px solid var(--rule);padding-top:1rem}
@media (max-width:960px){body{grid-template-columns:minmax(0,46rem);gap:0}
nav{position:static;height:auto;padding:1.4rem 0 0;border-bottom:1px solid var(--rule)}
main{padding-top:1.4rem}.hero h1{font-size:2rem}}
@media (prefers-reduced-motion:no-preference){nav .node{transition:background .2s}}
@media print{nav{display:none}body{display:block}.task{break-inside:avoid-page}}
"""

JS = r"""
(function(){
var KEY=document.body.dataset.plan+':';
var boxes=[].slice.call(document.querySelectorAll('input[data-box]'));
function load(b){var v=localStorage.getItem(KEY+b.dataset.box);b.checked=v===null?b.hasAttribute('data-default-checked'):v==='1';}
function update(){
  document.querySelectorAll('section.task').forEach(function(sec){
    var bs=sec.querySelectorAll('input[data-box]'),n=bs.length,d=0;
    bs.forEach(function(b){if(b.checked)d++;});
    var p=sec.querySelector('.prog');if(p)p.textContent=d+' of '+n+' steps';
    sec.classList.toggle('complete',n>0&&d===n);
    var li=document.querySelector('nav [data-task="'+sec.id+'"]');
    if(li){li.style.setProperty('--p',n?d/n:0);li.classList.toggle('done',n>0&&d===n);
      var c=li.querySelector('.cnt');if(c)c.textContent=d+'/'+n;}
  });
}
boxes.forEach(function(b){load(b);b.addEventListener('change',function(){localStorage.setItem(KEY+b.dataset.box,b.checked?'1':'0');update();});});
var clr=document.getElementById('clear');
if(clr)clr.addEventListener('click',function(){boxes.forEach(function(b){localStorage.removeItem(KEY+b.dataset.box);b.checked=false;});update();});
update();
})();
"""


def build(md_path, out_path):
    md = Path(md_path).read_bytes().decode("utf-8")
    key = Path(md_path).stem
    title, toc, body = convert(md, key)
    nav = [f'<p class="plan">{inline(title)}</p>', "<ol>"]
    in_route = False
    for kind, sid, n, text in toc:
        if kind == "task":
            if not in_route:
                nav.append('<li><ol class="route">')
                in_route = True
            nav.append(f'<li data-task="{sid}"><a href="#{sid}"><span class="node"><b>{n}</b></span>'
                       f'<span class="t">{inline(text)}</span><span class="cnt"></span></a></li>')
        else:
            if in_route:
                nav.append("</ol></li>")
                in_route = False
            nav.append(f'<li class="sec-link"><a href="#{sid}"><span class="t">{inline(text)}</span></a></li>')
    if in_route:
        nav.append("</ol></li>")
    nav.append("</ol>")
    nav.append('<button type="button" id="clear">Clear all checkmarks</button>')
    doc = (
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        f"<title>{html.escape(title)}</title>\n<style>{CSS}</style>\n</head>\n"
        f"<body data-plan=\"{html.escape(key)}\">\n<nav aria-label=\"Plan contents\">{''.join(nav)}</nav>\n"
        f"<main>\n{body}\n<footer>Generated from <code>{html.escape(Path(md_path).as_posix())}</code>. "
        "Checkmarks are saved in this browser only.</footer>\n</main>\n"
        f"<script>{JS}</script>\n</body>\n</html>\n"
    )
    Path(out_path).write_bytes(doc.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    return len(toc), sum(1 for k in toc if k[0] == "task")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: plan_html.py <plan.md> <out.html>")
    sections, tasks = build(sys.argv[1], sys.argv[2])
    print(f"wrote {sys.argv[2]}: {sections} sections, {tasks} tasks")
