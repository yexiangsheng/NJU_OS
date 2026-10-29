#!/usr/bin/env python3
'''visualize.py — 将 model-checker.py 的输出渲染成交互式 HTML 状态机图

用法:
    python3 model-checker.py mutex-bad.py | python3 visualize.py [输出.html]

从 stdin 读取 model-checker.py 打印的 CLASS/STATE/TRANS 行,
调用 graphviz (dot) 生成明/暗两套 SVG, 内嵌进一个自包含的 HTML:
  - 状态图: 节点 = 状态 (fill = @mc.marker 返回的颜色), 边 = 线程迁移
  - 状态表 / 插桩后源码 (带行号, 高亮所有可达 pc 行)
  - 悬停节点 <-> 表格行 联动高亮, 点击节点跳转到对应表格行

依赖: graphviz 的 dot 命令
'''

import ast, json, re, subprocess, sys
from pathlib import Path

# ---------------- 解析 model-checker.py 输出 ----------------

def parse_lines(text):
    '''解析 CLASS(...)/STATE(...)/TRANS(...) 行'''
    cls_src, states, trans = None, {}, []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            call = ast.parse(line, mode='eval').body
            if not (isinstance(call, ast.Call) and isinstance(call.func, ast.Name)):
                continue
            args = [ast.literal_eval(a) for a in call.args]
        except SyntaxError:
            continue  # 跳过非模型检查输出
        if call.func.id == 'CLASS' and args:
            cls_src = args[0]
        elif call.func.id == 'STATE' and len(args) == 3:
            states[args[0]] = (args[1], args[2])  # name -> (statedict, markers)
        elif call.func.id == 'TRANS' and len(args) == 3:
            trans.append(tuple(args))  # (u, v, thread)
    return cls_src, states, trans

# ---------------- 小工具 ----------------

def natkey(s):
    '''t1 < t2 < t10 的自然排序键'''
    return [int(p) if p.isdigit() else p for p in re.split(r'(\d+)', s)]

def esc(s):
    return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def fmt(v, limit=24):
    s = repr(v)
    return s if len(s) <= limit else s[:limit - 1] + '…'

def class_name(src):
    m = re.search(r'^class\s+(\w+)', src or '', re.M)
    return m.group(1) if m else 'Machine'

# ---------------- 主题 (取自已验证的调色板) ----------------

THEMES = {
    'light': dict(
        ink='#0b0b0b', sub='#52514e', muted='#898781',
        surface='#fcfcfb',
        # 分类色槽位: 按线程固定顺序分配 (边颜色 / 线程徽标)
        cat=['#2a78d6', '#008300', '#e87ba4', '#eda100',
             '#1baf7a', '#eb6834', '#4a3aa7'],
        # marker 颜色 -> (浅色填充, 描边); red = 违反互斥 (critical)
        marker={'blue':  ('#9ec5f4', '#2a78d6'),
                'green': ('#b5dfb5', '#008300'),
                'red':   ('#f3c3c3', '#d03b3b'),
                'yellow': ('#f8dfae', '#c98500')},
    ),
    'dark': dict(
        ink='#ffffff', sub='#c3c2b7', muted='#898781',
        surface='#1a1a19',
        cat=['#3987e5', '#008300', '#d55181', '#c98500',
             '#199e70', '#d95926', '#9085e9'],
        marker={'blue':  ('#184f95', '#3987e5'),
                'green': ('#0d5c0d', '#0ca30c'),
                'red':   ('#5a2323', '#e66767'),
                'yellow': ('#6b4a00', '#eda100')},
    ),
}
MARKER_PRIORITY = ['red', 'blue', 'green', 'yellow']

def marker_style(markers, th):
    '''多个 marker 命中时 red 优先'''
    for m in MARKER_PRIORITY:
        if m in markers:
            return th['marker'][m]
    return None

def thread_colors(threads, th):
    '''线程 -> 分类色; 超过 7 个折叠为中性灰'''
    cat = th['cat']
    return {t: cat[i] if i < len(cat) else th['muted']
            for i, t in enumerate(threads)}

# ---------------- 生成 dot ----------------

def dot_label(name, statedict, markers, threads, tc, th):
    '''节点标签: 状态名 + 每线程 pc/局部变量 + 共享变量'''
    rows = [f'<TR><TD ALIGN="CENTER"><B>{esc(name)}</B></TD></TR>']
    for t in threads:
        chip = f'<FONT COLOR="{tc[t]}">■</FONT>'
        if t in statedict:
            pc, locs = statedict[t]
            loc = ', '.join(f'{k}={fmt(v)}' for k, v in locs.items())
            text = f'{chip} {esc(t)}&nbsp;&nbsp;L{pc}'
            if loc:
                text += f'&nbsp;&nbsp;<FONT COLOR="{th["muted"]}">{esc(loc)}</FONT>'
        else:
            text = f'{chip} {esc(t)}&nbsp;&nbsp;<FONT COLOR="{th["muted"]}">(结束)</FONT>'
        rows.append(f'<TR><TD ALIGN="LEFT">{text}</TD></TR>')
    for k, v in statedict.items():
        if k not in threads:
            rows.append(f'<TR><TD ALIGN="LEFT"><FONT COLOR="{th["muted"]}">'
                        f'{esc(k)}={esc(fmt(v))}</FONT></TD></TR>')
    return ('<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="0" CELLPADDING="2">'
            + ''.join(rows) + '</TABLE>>')

def build_dot(states, trans, threads, th):
    tc = thread_colors(threads, th)
    L = ['digraph G {',
         '  bgcolor="transparent"',
         '  graph [fontname="Helvetica", rankdir=LR, ranksep=0.4, nodesep=0.3, splines=true];',
         f'  node [shape=box, style="filled,rounded", fontname="Helvetica", '
         f'fontsize=11, fontcolor="{th["ink"]}", color="{th["muted"]}", '
         f'fillcolor="{th["surface"]}", penwidth=1.1, margin="0.11,0.07"];',
         f'  edge [fontname="Helvetica", fontsize=9, fontcolor="{th["sub"]}", '
         f'penwidth=1.3, arrowsize=0.7];',
         # 入口: 加粗箭头指向初始状态
         f'  __start [shape=circle, width=0.07, style=filled, '
         f'fillcolor="{th["sub"]}", color="{th["sub"]}", label=""];']

    for name, (statedict, markers) in states.items():
        attrs = [f'label={dot_label(name, statedict, markers, threads, tc, th)}']
        st = marker_style(markers, th)
        if st:
            fill, border = st
            attrs.append(f'fillcolor="{fill}"')
            attrs.append(f'color="{border}"')
            if 'red' in markers:
                attrs.append('penwidth=2, style="filled,rounded,bold"')
        L.append(f'  "{name}" [{", ".join(attrs)}];')

    if states:
        s0 = next(iter(states))
        L.append(f'  __start -> "{s0}" [color="{th["sub"]}", style=bold, penwidth=1.5];')

    for u, v, t in trans:
        L.append(f'  "{u}" -> "{v}" [label="{esc(t)}", color="{tc[t]}"];')

    L.append('}')
    return '\n'.join(L)

def run_dot(dot_src):
    p = subprocess.run(['dot', '-Tsvg'], input=dot_src.encode('utf-8'),
                       capture_output=True)
    if p.returncode != 0:
        sys.exit(f'dot 渲染失败:\n{p.stderr.decode("utf-8", "replace")}')
    return p.stdout.decode('utf-8')

def tune_svg(svg, prefix):
    '''去掉 xml 头, 摘除固定宽高 (响应式), id 加前缀避免明暗两份冲突'''
    svg = re.sub(r'<\?xml[^>]*\?>', '', svg)
    svg = re.sub(r'<!DOCTYPE[^>]*>', '', svg)
    svg = re.sub(r'(<svg[^>]*?)\swidth="[^"]*"', r'\1', svg)
    svg = re.sub(r'(<svg[^>]*?)\sheight="[^"]*"', r'\1', svg)
    svg = re.sub(r'\bid="', f'id="{prefix}', svg)
    svg = re.sub(r'(url\(#|href="#|xlink:href="#)', lambda m: m.group(1) + prefix, svg)
    return svg.strip()

# ---------------- HTML 片段 ----------------

def chip(t, tc_light, tc_dark):
    return f'<span class="chip" style="--c-l:{tc_light};--c-d:{tc_dark}"></span>'

def tooltip_html(name, statedict, markers, threads, tcl, tcd):
    parts = [f'<div class="tt-name">{esc(name)}</div>']
    if markers:
        dots = ' '.join(f'<span class="mdot mk-{esc(m)}"></span>{esc(m)}'
                        for m in markers)
        parts.append(f'<div class="tt-mk">markers: {dots}</div>')
    for t in threads:
        c = chip(t, tcl[t], tcd[t])
        if t in statedict:
            pc, locs = statedict[t]
            loc = ', '.join(f'{k}={v!r}' for k, v in locs.items()) or '{}'
            parts.append(f'<div>{c}{esc(t)}&nbsp;L{pc}&nbsp;{esc(loc)}</div>')
        else:
            parts.append(f'<div>{c}{esc(t)}&nbsp;<i>(结束)</i></div>')
    for k, v in statedict.items():
        if k not in threads:
            parts.append(f'<div class="tt-sh">{esc(k)}={esc(repr(v))}</div>')
    return ''.join(parts)

def table_rows(states, threads, tcl, tcd):
    out = []
    for name, (statedict, markers) in states.items():
        red = 'red' in markers
        mk = ' '.join(f'<span class="mdot mk-{esc(m)}" title="marker: {esc(m)}"></span>'
                      for m in markers) or '<span class="muted">—</span>'
        tds = []
        for t in threads:
            c = chip(t, tcl[t], tcd[t])
            if t in statedict:
                pc, locs = statedict[t]
                loc = ', '.join(f'{k}={fmt(v, 40)}' for k, v in locs.items())
                line = f'{c}{esc(t)}: L{pc}' + (f'&nbsp;<span class="loc">{esc(loc)}</span>' if loc else '')
            else:
                line = f'{c}{esc(t)}: <i>(结束)</i>'
            tds.append(f'<div>{line}</div>')
        shared = [f'<div>{esc(k)}={esc(fmt(v, 60))}</div>'
                  for k, v in statedict.items() if k not in threads]
        out.append(
            f'<tr id="row-{esc(name)}" data-state="{esc(name)}"'
            f'{" class=red-row" if red else ""}>'
            f'<td class="mono">{esc(name)}</td>'
            f'<td>{mk}</td>'
            f'<td>{"".join(tds)}</td>'
            f'<td class="mono shared">{"".join(shared) or "<span class=muted>—</span>"}</td></tr>')
    return '\n'.join(out)

def source_html(cls_src, states, threads):
    '''插桩源码: 行号 + 高亮所有出现过的 pc 行; 调暗 yield checkpoint()'''
    pcs = {sd[t][0] for sd, _ in states.values() for t in threads if t in sd}
    lines = (cls_src or '').splitlines()
    out = []
    for i, line in enumerate(lines, 1):
        cls = []
        if i in pcs: cls.append('pc')
        if 'yield checkpoint()' in line: cls.append('cp')
        c = f' class="{" ".join(cls)}"' if cls else ''
        out.append(f'<span class="ln">{i}</span><span{c}>{esc(line) or " "}</span>')
    return '\n'.join(out)

# ---------------- 页面模板 ----------------

PAGE = '''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>状态机可视化 — __TITLE__</title>
<style>
  :root {
    color-scheme: light;
    --page: #f9f9f7; --surface: #fcfcfb;
    --ink: #0b0b0b; --sub: #52514e; --muted: #898781;
    --hair: #e1e0d9; --border: rgba(11,11,11,0.10);
    --critical: #d03b3b; --good: #0ca30c;
    --wash: rgba(42,120,214,0.08);
  }
  @media (prefers-color-scheme: dark) {
    :root { color-scheme: dark;
      --page: #0d0d0d; --surface: #1a1a19;
      --ink: #ffffff; --sub: #c3c2b7; --muted: #898781;
      --hair: #2c2c2a; --border: rgba(255,255,255,0.10);
      --wash: rgba(57,135,229,0.14); }
  }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 24px; background: var(--page); color: var(--ink);
         font: 14px/1.6 system-ui, -apple-system, "Segoe UI", sans-serif; }
  .wrap { max-width: 1200px; margin: 0 auto; }
  h1 { font-size: 20px; margin: 0 0 4px; }
  .sub { color: var(--sub); margin: 0 0 20px; }
  .tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
           gap: 12px; margin-bottom: 20px; }
  .tile { background: var(--surface); border: 1px solid var(--border);
          border-radius: 10px; padding: 12px 16px; }
  .tile .k { font-size: 12px; color: var(--muted); }
  .tile .v { font-size: 26px; font-weight: 600; line-height: 1.3; }
  .tile .v.crit { color: var(--critical); }
  .card { background: var(--surface); border: 1px solid var(--border);
          border-radius: 10px; padding: 16px 20px; margin-bottom: 20px; }
  .card h2 { font-size: 15px; margin: 0 0 12px; color: var(--sub);
             font-weight: 600; letter-spacing: 0.02em; }
  .banner { display: none; border: 1px solid rgba(208,59,59,0.35);
            background: rgba(208,59,59,0.08); color: var(--ink);
            border-radius: 10px; padding: 10px 16px; margin-bottom: 20px; }
  .banner .warn { color: var(--critical); font-weight: 600; margin-right: 8px; }
  .legend { display: flex; flex-wrap: wrap; gap: 18px; align-items: center;
            font-size: 13px; }
  .legend .grp { display: flex; align-items: center; gap: 6px; }
  .legend .cap { color: var(--muted); margin-right: 2px; }
  .chip { display: inline-block; width: 10px; height: 10px; border-radius: 3px;
          background: var(--c-l); border: 1px solid var(--border); margin-right: 6px;
          vertical-align: baseline; }
  @media (prefers-color-scheme: dark) { .chip { background: var(--c-d); } }
  .mdot { display: inline-block; width: 10px; height: 10px; border-radius: 50%;
          margin-right: 4px; border: 1px solid var(--border); }
  .mk-blue { background: #9ec5f4; } .mk-green { background: #b5dfb5; }
  .mk-red { background: #f3c3c3; } .mk-yellow { background: #f8dfae; }
  @media (prefers-color-scheme: dark) {
    .mk-blue { background: #184f95; } .mk-green { background: #0d5c0d; }
    .mk-red { background: #5a2323; } .mk-yellow { background: #6b4a00; } }
  .gv-stage { position: relative; overflow: hidden; border: 1px solid var(--hair);
              border-radius: 8px; touch-action: none; user-select: none;
              cursor: grab; background: var(--surface); }
  .gv-stage.panning { cursor: grabbing; }
  .gv svg { width: 100%; height: auto; display: block; }
  .gv-tools { display: flex; gap: 8px; align-items: center; margin-bottom: 10px; }
  .gv-tools button { font: 13px/1 system-ui, sans-serif; padding: 5px 10px;
         border-radius: 6px; border: 1px solid var(--hair); background: var(--surface);
         color: var(--ink); cursor: pointer; }
  .gv-tools button:hover { border-color: var(--muted); }
  .gv-tools .hint { color: var(--muted); font-size: 12px; margin-left: auto; }
  .gv-dark { display: none; }
  @media (prefers-color-scheme: dark) { .gv-light { display: none; }
                                        .gv-dark { display: block; } }
  .gv g.node { cursor: pointer; }
  .gv g.node.hl ellipse, .gv g.node.hl polygon { stroke-width: 3; }
  table { border-collapse: collapse; width: 100%; font-size: 13px; }
  th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--hair);
           vertical-align: top; }
  thead th { position: sticky; top: 0; background: var(--surface); color: var(--muted);
             font-weight: 600; font-size: 12px; }
  tbody tr:hover, tbody tr.active { background: var(--wash); }
  tr.red-row td:first-child { box-shadow: inset 3px 0 0 var(--critical); }
  tr.flash { animation: flash 1.2s ease-out; }
  @keyframes flash { 0% { background: var(--wash); } 100% { background: transparent; } }
  .mono, .loc { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
  td.shared { color: var(--sub); }
  .muted { color: var(--muted); }
  .tbl-card { max-height: 480px; overflow: auto; }
  pre.src { margin: 0; font: 12px/1.7 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
            overflow-x: auto; }
  pre.src span { display: inline-block; min-width: 100%; }
  pre.src .ln { display: inline-block; width: 2.5em; color: var(--muted);
                text-align: right; margin-right: 1.2em; user-select: none; }
  pre.src .pc { background: var(--wash); border-radius: 3px; }
  pre.src .cp { color: var(--muted); }
  #tip { position: fixed; z-index: 10; max-width: 420px; pointer-events: none;
         background: var(--surface); border: 1px solid var(--border); border-radius: 8px;
         padding: 10px 12px; font-size: 12.5px; box-shadow: 0 4px 16px rgba(0,0,0,0.18);
         font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
  #tip[hidden] { display: none; }
  .tt-name { font-weight: 700; margin-bottom: 4px; }
  .tt-mk { margin-bottom: 4px; }
  .tt-sh { color: var(--sub); }
  footer { color: var(--muted); font-size: 12px; text-align: center; }
</style>
</head>
<body>
<div class="wrap">
  <h1>状态机可视化 — __TITLE__</h1>
  <p class="sub">model-checker.py 输出 · 悬停节点查看详情, 点击跳转表格行</p>

  <div class="tiles">
    <div class="tile"><div class="k">状态</div><div class="v">__NSTATES__</div></div>
    <div class="tile"><div class="k">迁移</div><div class="v">__NTRANS__</div></div>
    <div class="tile"><div class="k">线程</div><div class="v" style="font-size:17px;padding-top:5px">__THREADS__</div></div>
    <div class="tile"><div class="k">红色标记状态</div><div class="v__CRITCLS__">__NRED__</div></div>
  </div>

  <div class="banner" id="banner"><span class="warn">⚠</span>__BANNER__</div>

  <div class="card">
    <h2>图例</h2>
    <div class="legend">
      <span class="cap">节点填充 = marker:</span>__MKLEGEND__
      <span class="cap">边 / ■ = 线程:</span>__THLEGEND__
      <span class="cap">入口箭头 = 初始状态 s0</span>
    </div>
  </div>

  <div class="card">
    <h2>状态迁移图</h2>
    <div class="gv-tools">
      <button id="zoom-out" title="缩小">−</button>
      <button id="zoom-in" title="放大">＋</button>
      <button id="zoom-fit" title="重置视图">⟲ 重置</button>
      <span class="hint">滚轮缩放 · 拖动平移 · 悬停节点查看详情 · 点击节点跳转表格</span>
    </div>
    <div class="gv-stage gv" id="stage">
      <div class="gv-light">__SVG_LIGHT__</div>
      <div class="gv-dark">__SVG_DARK__</div>
    </div>
  </div>

  <div class="card tbl-card">
    <h2>状态表</h2>
    <table id="states-table">
      <thead><tr><th>状态</th><th>markers</th><th>线程 (pc · 局部变量)</th><th>共享变量</th></tr></thead>
      <tbody>
__ROWS__
      </tbody>
    </table>
  </div>

  <div class="card">
    <h2>插桩后源码 (行号即状态中的 pc; 高亮行 = 可达 pc; 灰色 = yield checkpoint())</h2>
    <pre class="src"><code>__SOURCE__</code></pre>
  </div>

  <footer>由 visualize.py 生成 · 输入来自 model-checker.py 管道</footer>
</div>

<div id="tip" hidden></div>
<script>
const DATA = __DATA__;
const tip = document.getElementById('tip');
const byName = {};

// ---- 缩放 / 平移 (操作 viewBox, 明暗两份同步) ----
const stage = document.getElementById('stage');
const svgs = [...stage.querySelectorAll('svg')];
let vw = 0, vh = 0;
svgs.forEach(s => { const v = s.viewBox.baseVal;
                    vw = Math.max(vw, v.width); vh = Math.max(vh, v.height); });
let scale = 1, ox = 0, oy = 0, justDragged = false;
function applyView() {
  const w = vw / scale, h = vh / scale;
  ox = Math.min(Math.max(ox, 0), Math.max(0, vw - w));
  oy = Math.min(Math.max(oy, 0), Math.max(0, vh - h));
  svgs.forEach(s => s.setAttribute('viewBox', `${ox} ${oy} ${w} ${h}`));
}
function zoomAt(cx, cy, factor) {  // cx/cy = 指针在画布中的比例位置
  const w1 = vw / scale, h1 = vh / scale;
  const fx = ox + cx * w1, fy = oy + cy * h1;
  scale = Math.min(8, Math.max(0.3, scale * factor));
  ox = fx - cx * vw / scale; oy = fy - cy * vh / scale;
  applyView();
}
stage.addEventListener('wheel', e => {
  e.preventDefault();
  const r = stage.getBoundingClientRect();
  zoomAt((e.clientX - r.left) / r.width, (e.clientY - r.top) / r.height,
         e.deltaY < 0 ? 1.18 : 1 / 1.18);
}, { passive: false });
let drag = null;
stage.addEventListener('pointerdown', e => {
  drag = { x: e.clientX, y: e.clientY, ox, oy, moved: 0 };
  stage.setPointerCapture(e.pointerId);
});
stage.addEventListener('pointermove', e => {
  if (!drag) return;
  const r = stage.getBoundingClientRect();
  ox = drag.ox - (e.clientX - drag.x) / r.width * vw / scale;
  oy = drag.oy - (e.clientY - drag.y) / r.height * vh / scale;
  drag.moved = Math.max(drag.moved, Math.abs(e.clientX - drag.x)
                                  + Math.abs(e.clientY - drag.y));
  stage.classList.toggle('panning', drag.moved > 3);
  applyView();
});
['pointerup', 'pointercancel'].forEach(ev => stage.addEventListener(ev, () => {
  justDragged = drag && drag.moved > 5;
  stage.classList.remove('panning');
  drag = null;
}));
document.getElementById('zoom-in').onclick = () => zoomAt(0.5, 0.5, 1.3);
document.getElementById('zoom-out').onclick = () => zoomAt(0.5, 0.5, 1 / 1.3);
document.getElementById('zoom-fit').onclick = () => { scale = 1; ox = oy = 0; applyView(); };


function hl(name, on) {
  (byName[name] || []).forEach(g => g.classList.toggle('hl', on));
  const tr = document.getElementById('row-' + name);
  if (tr) tr.classList.toggle('active', on);
}
document.querySelectorAll('.gv g.node').forEach(g => {
  const t = g.querySelector('title');
  if (!t) return;
  const name = t.textContent.trim();
  (byName[name] = byName[name] || []).push(g);
  g.addEventListener('mousemove', e => {
    const x = Math.min(e.clientX + 16, innerWidth - tip.offsetWidth - 8);
    const y = Math.min(e.clientY + 16, innerHeight - tip.offsetHeight - 8);
    tip.style.left = x + 'px'; tip.style.top = y + 'px';
  });
  g.addEventListener('mouseenter', () => {
    tip.innerHTML = DATA[name] || name; tip.hidden = false; hl(name, true);
  });
  g.addEventListener('mouseleave', () => { tip.hidden = true; hl(name, false); });
  g.addEventListener('click', () => {
    if (justDragged) return;  // 拖动结束不触发跳转
    const tr = document.getElementById('row-' + name);
    if (!tr) return;
    tr.scrollIntoView({behavior: matchMedia('(prefers-reduced-motion: reduce)')
                      .matches ? 'auto' : 'smooth', block: 'center'});
    tr.classList.remove('flash'); void tr.offsetWidth; tr.classList.add('flash');
  });
});
document.querySelectorAll('#states-table tbody tr').forEach(tr => {
  tr.addEventListener('mouseenter', () => hl(tr.dataset.state, true));
  tr.addEventListener('mouseleave', () => hl(tr.dataset.state, false));
});
const nRed = __NRED__;
if (nRed > 0) document.getElementById('banner').style.display = 'block';
</script>
</body>
</html>
'''

# ---------------- 主流程 ----------------

def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else 'state-machine.html'

    cls_src, states, trans = parse_lines(sys.stdin.read())
    if not states:
        sys.exit('未从 stdin 解析到 STATE/TRANS 行 — 请确认输入来自 '
                 '`python3 model-checker.py <model.py>` 的管道输出')

    threads = sorted({t for _, _, t in trans}, key=natkey)
    if not threads:  # 没有迁移时退化为按状态键猜测
        threads = sorted({k for sd, _ in states.values()
                          for k, v in sd.items()
                          if isinstance(v, tuple) and len(v) == 2
                          and isinstance(v[0], int) and isinstance(v[1], dict)},
                         key=natkey)

    light, dark = THEMES['light'], THEMES['dark']
    tcl, tcd = thread_colors(threads, light), thread_colors(threads, dark)

    svg_light = tune_svg(run_dot(build_dot(states, trans, threads, light)), 'l-')
    svg_dark = tune_svg(run_dot(build_dot(states, trans, threads, dark)), 'd-')

    red_states = [n for n, (_, mk) in states.items() if 'red' in mk]
    crit_cls = ' crit' if red_states else ''
    banner = (f'发现 {len(red_states)} 个带红色标记的状态 (两个线程同时处于临界区): '
              + ', '.join(red_states[:12])
              + (' …' if len(red_states) > 12 else '')) if red_states else ''

    mk_legend = ''.join(
        f'<span class="grp"><span class="mdot mk-{m}"></span>{m}</span>'
        for m in ['blue', 'green', 'red']
        if any(m in mk for _, mk in states.values()))
    th_legend = ''.join(
        f'<span class="grp">{chip(t, tcl[t], tcd[t])}{esc(t)}</span>'
        for t in threads)

    data = {name: tooltip_html(name, sd, mk, threads, tcl, tcd)
            for name, (sd, mk) in states.items()}
    data_json = json.dumps(data, ensure_ascii=False).replace('<', '\\u003c')

    html_page = (PAGE
        .replace('__TITLE__', esc(class_name(cls_src)))
        .replace('__NSTATES__', str(len(states)))
        .replace('__NTRANS__', str(len(trans)))
        .replace('__THREADS__', esc(', '.join(threads)))
        .replace('__CRITCLS__', crit_cls)
        .replace('__NRED__', str(len(red_states)))
        .replace('__BANNER__', esc(banner))
        .replace('__MKLEGEND__', mk_legend or '<span class="muted">无</span>')
        .replace('__THLEGEND__', th_legend)
        .replace('__SVG_LIGHT__', svg_light)
        .replace('__SVG_DARK__', svg_dark)
        .replace('__ROWS__', table_rows(states, threads, tcl, tcd))
        .replace('__SOURCE__', source_html(cls_src, states, threads))
        .replace('__DATA__', data_json))

    Path(out_path).write_text(html_page, encoding='utf-8')
    print(f'已生成 {out_path}: {len(states)} 状态, {len(trans)} 迁移, '
          f'线程 {", ".join(threads)}, {len(red_states)} 个红色标记状态',
          file=sys.stderr)

if __name__ == '__main__':
    main()
