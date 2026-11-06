# 汉诺塔可视化:读入 "A -> B" 这样的移动记录(每行一条),逐帧动画重放
# 用法:  ./hanoi 3 A B C | python3 hanoi-vis.py
#   或:  python3 hanoi-vis.py moves.txt     (支持回放/慢放,同 seven-seg.py 的 fileinput)
import fileinput, re, sys, math, time

SPEED = 0.15                # 两帧间隔(秒),想慢放改大
CLEAR = '\033[2J\033[1;1f'  # 清屏 + 光标回家,同 seven-seg.py
RESET = '\033[0m'
COLOR = ['\033[31m', '\033[32m', '\033[33m', '\033[34m', '\033[35m', '\033[36m']

# ---- 先读完所有输入:n 步汉诺塔共 2^n - 1 步,据此反推盘子数 n ----
moves = []
for line in fileinput.input():
    clean = re.sub(r'\033\[[0-9;]*m', '', line)   # 去掉 C 程序输出的 ANSI 颜色码
    m = re.search(r'([A-C])\s*->\s*([A-C])', clean)
    if m:
        moves.append((m.group(1), m.group(2)))

if not moves or 2 ** round(math.log2(len(moves) + 1)) - 1 != len(moves):
    sys.exit(f'共 {len(moves)} 行,不是 2^n-1 条移动记录,输入不对')
n = round(math.log2(len(moves) + 1))

# ---- 状态:n 个盘子全在 A 上。每根柱子是一个栈,底大顶小 ----
pegs = {c: [] for c in 'ABC'}          # 先都置空,注意不能给每根柱子都发一套盘子
pegs['A'] = list(range(n, 0, -1))      # 只有 A 有 [n, n-1, ..., 1](栈顶=最小的 1)
W = 2 * n + 2                # 每根柱子的画布宽度

def frame():
    '''把当前状态(三个栈)画成一帧 —— 程序=状态机,画面是状态的投影'''
    lines = [CLEAR]
    for level in range(n, 0, -1):          # 从顶层画到底层
        row = ''
        for c in 'ABC':
            s = pegs[c]
            if level <= len(s):            # 这一层有盘子,宽度 = 2*大小-1,按大小着色
                d = s[level - 1]
                row += COLOR[(d - 1) % len(COLOR)] + ('#' * (2 * d - 1)).center(W) + RESET
            else:
                row += '|'.center(W)       # 空层画柱子
        lines.append(row)
    lines.append(''.join(c.center(W) for c in 'ABC'))
    lines.append('=' * 3 * W)
    return '\n'.join(lines)

def show():
    print(frame(), flush=True)

show()                                     # 初始局面
for i, (frm, to) in enumerate(moves, 1):
    if not pegs[frm]:
        sys.exit(f'第 {i} 步 {frm} -> {to} 非法:柱子 {frm} 上没有盘子')
    pegs[to].append(pegs[frm].pop())       # 状态转移:从 from 弹出,压入 to
    show()
    time.sleep(SPEED)

for c in 'ABC':
    if len(pegs[c]) == n:
        print(f'{len(moves)} 步后,{n} 个盘子全部在柱子 {c} 上')
