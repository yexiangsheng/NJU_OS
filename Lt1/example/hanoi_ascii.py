#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 ASCII 字符动画演示汉诺塔的递归求解过程。

每移动一个盘子就重绘一次三根柱子的俯视图, 盘子用 `=N=` 形式的横条表示,
数字越大盘子越宽。加 --trace 可以同时看到递归调用栈的展开与回溯。

用法:
    python3 hanoi_ascii.py             # 默认 3 个盘子
    python3 hanoi_ascii.py 4           # 4 个盘子
    python3 hanoi_ascii.py 4 --trace   # 额外显示递归调用栈
    python3 hanoi_ascii.py 5 --delay 0.1
    python3 hanoi_ascii.py 3 --no-color --no-clear   # 逐帧打印, 不清屏
"""

import argparse
import sys
import time

PEG_NAMES = "ABC"

# ANSI 前景色编号, 按盘子大小循环取色
PALETTE = (96, 93, 92, 91, 95, 94, 33, 36, 32, 34)
RESET, BOLD, DIM = 0, 1, 2


def esc(code):
    return "\033[%dm" % code


def hanoi_events(n, src, dst, aux, depth=0):
    """递归生成事件序列: enter(进入函数) / move(移动盘子) / exit(函数返回)."""
    label = "hanoi(%d, %s -> %s, 借助 %s)" % (n, src, dst, aux)
    yield "enter", depth, label
    if n > 0:
        yield from hanoi_events(n - 1, src, aux, dst, depth + 1)
        yield "move", depth, (n, src, dst)
        yield from hanoi_events(n - 1, aux, dst, src, depth + 1)
    yield "exit", depth, label


class View:
    """把三根柱子的状态画成 ASCII 图."""

    def __init__(self, n, color=True):
        self.n = n
        self.col = 2 * n + 1   # 单根柱子区域的宽度 (最宽盘子 + 居中余量)
        self.gap = "   "
        self.color = color

    def paint(self, text, *codes):
        if not self.color or not codes:
            return text
        return "".join(esc(c) for c in codes) + text + esc(RESET)

    def render(self, pegs, step, total, move=None, stack=(), note=None):
        lines = []
        header = "汉诺塔(%d 个盘子)  第 %d/%d 步" % (self.n, step, total)
        if move:
            disk, src, dst = move
            header += "   盘子 %d: %s -> %s" % (disk, src, dst)
        lines.append(self.paint(header, BOLD))
        lines.append("")

        # 从顶层往底行画, 每根柱子一层
        for level in range(self.n - 1, -1, -1):
            cells = []
            for name in PEG_NAMES:
                tower = pegs[name]
                if level < len(tower):
                    disk = tower[level]
                    bar = ("=" * disk + str(disk) + "=" * disk).center(self.col)
                    cells.append(self.paint(bar, PALETTE[(disk - 1) % len(PALETTE)]))
                else:
                    cells.append(self.paint("|".center(self.col), DIM))
            lines.append(self.gap.join(cells))

        lines.append(self.gap.join("-" * self.col for _ in PEG_NAMES))
        lines.append(self.gap.join(name.center(self.col) for name in PEG_NAMES))

        if stack:
            lines.append("")
            lines.append(self.paint("递归调用栈 (缩进越深, 调用越内层):", BOLD))
            for depth, label in enumerate(stack):
                lines.append("  " * depth + label)

        if note:
            lines.append("")
            lines.append(self.paint(note, BOLD))
        return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(
        description="ASCII 动画演示汉诺塔递归过程",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("n", nargs="?", type=int, default=3,
                    help="盘子数量 (1-10)")
    ap.add_argument("--delay", type=float, default=0.4,
                    help="每步停留秒数")
    ap.add_argument("--trace", action="store_true",
                    help="同时显示递归调用栈的展开/回溯")
    ap.add_argument("--no-color", action="store_true", help="关闭 ANSI 颜色")
    ap.add_argument("--no-clear", action="store_true", help="不清屏, 逐帧打印")
    args = ap.parse_args()

    if not 1 <= args.n <= 10:
        sys.exit("盘子数量请在 1 ~ 10 之间 (宽度受终端限制)")

    total = 2 ** args.n - 1
    tty = sys.stdout.isatty()
    animate = tty and not args.no_clear
    color = tty and not args.no_color
    delay = args.delay if tty else 0.0

    view = View(args.n, color)
    pegs = {name: [] for name in PEG_NAMES}
    pegs["A"] = list(range(args.n, 0, -1))   # 底部在下标 0, 顶端在末尾

    step, last_move, stack = 0, None, []

    def show(note=None):
        if animate:
            sys.stdout.write("\033[H\033[2J\033[3J")
        print(view.render(pegs, step, total, move=last_move,
                          stack=stack if args.trace else (), note=note))
        if not animate:
            print()
        sys.stdout.flush()

    if animate:
        sys.stdout.write("\033[?25l")        # 藏起光标, 免得闪烁

    try:
        show()
        time.sleep(delay)
        for kind, depth, payload in hanoi_events(args.n, "A", "C", "B"):
            if kind == "enter":
                stack.append(payload)
                if args.trace:
                    show()
                    time.sleep(delay / 2)
            elif kind == "exit":
                stack.pop()
                if args.trace:
                    show()
                    time.sleep(delay / 2)
            else:                            # move
                disk, src, dst = payload
                pegs[src].pop()
                pegs[dst].append(disk)
                step += 1
                last_move = payload
                show()
                time.sleep(delay)
        show(note="完成! 共 %d 步 (2^%d - 1 = %d)" % (step, args.n, total))
    except KeyboardInterrupt:
        print()
    finally:
        if animate:
            sys.stdout.write("\033[?25h")    # 恢复光标
            sys.stdout.flush()


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:                 # 输出被管道截断 (如 | head) 时安静退出
        import os
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, sys.stdout.fileno())
        sys.exit(1)
