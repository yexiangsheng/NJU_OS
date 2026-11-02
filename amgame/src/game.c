#include <game.h>

// 贪吃蛇：方向键/WASD 转向，死后空格重开，ESC 退出
#define SIDE 16
#define CAP  (64 * 48)        // 蛇身长度上限（覆盖 1024x768 屏幕）

#define BG   0x10141f
#define HEAD 0x8ce99a
#define BODY 0x2f9e44
#define FOOD 0xe8433f
#define DEAD 0xd63947

static int cols, rows;
static int px[CAP], py[CAP], len;   // px[0] 是头
static int dx, dy, fx, fy, dead;
static unsigned long long rng = 88172645463325252ull;

static unsigned long long ms() { return io_read(AM_TIMER_UPTIME).us / 1000; }

static int rnd(int n) {
  rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
  return rng % n;
}

static void draw_cell(int cx, int cy, uint32_t color) {
  static uint32_t buf[SIDE * SIDE];
  for (int i = 0; i < SIDE * SIDE; i++) buf[i] = color;
  io_write(AM_GPU_FBDRAW, cx * SIDE, cy * SIDE, buf, SIDE, SIDE, true);
}

static int on_body(int x, int y, int upto) {
  for (int i = 0; i < upto; i++)
    if (px[i] == x && py[i] == y) return 1;
  return 0;
}

static void place_food() {
  do { fx = rnd(cols); fy = rnd(rows); } while (on_body(fx, fy, len));
  draw_cell(fx, fy, FOOD);
}

static void new_game() {
  len = 3; dx = 1; dy = 0; dead = 0;
  for (int x = 0; x < cols; x++)
    for (int y = 0; y < rows; y++)
      draw_cell(x, y, BG);
  for (int i = 0; i < len; i++) {
    px[i] = cols / 2 - i; py[i] = rows / 2;
    draw_cell(px[i], py[i], i ? BODY : HEAD);
  }
  place_food();
}

static void step() {
  if (dead) return;
  int nx = px[0] + dx, ny = py[0] + dy;
  int eat = nx == fx && ny == fy;
  // 撞墙 / 撞自己（不吃时尾巴会让出格子，不算最后一节）
  if (nx < 0 || nx >= cols || ny < 0 || ny >= rows ||
      on_body(nx, ny, eat ? len : len - 1)) {
    dead = 1;
    draw_cell(px[0], py[0], DEAD);
    puts("You died! press SPACE to restart\n");
    return;
  }
  if (!eat) draw_cell(px[len - 1], py[len - 1], BG); // 擦掉尾巴
  else len++;
  for (int i = len - 1; i > 0; i--) { px[i] = px[i - 1]; py[i] = py[i - 1]; }
  px[0] = nx; py[0] = ny;
  draw_cell(px[1], py[1], BODY); // 原来的头变成身体
  draw_cell(nx, ny, HEAD);
  if (eat) place_food();
}

int main(const char *args)
{
  ioe_init();
  puts("Snake: arrows/WASD to turn, ESC to quit\n");

  AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
  cols = cfg.width / SIDE;
  rows = cfg.height / SIDE;
  if (cols > 64) cols = 64;
  if (rows > 48) rows = 48;
  new_game();

  unsigned long long next = ms();
  while (1) {
    AM_INPUT_KEYBRD_T k = io_read(AM_INPUT_KEYBRD);
    if (k.keydown) {
      switch (k.keycode) {
        case AM_KEY_UP: case AM_KEY_W:
          if (dy != 1) { dx = 0; dy = -1; } break;
        case AM_KEY_DOWN: case AM_KEY_S:
          if (dy != -1) { dx = 0; dy = 1; } break;
        case AM_KEY_LEFT: case AM_KEY_A:
          if (dx != 1) { dx = -1; dy = 0; } break;
        case AM_KEY_RIGHT: case AM_KEY_D:
          if (dx != -1) { dx = 1; dy = 0; } break;
        case AM_KEY_SPACE:
          if (dead) { new_game(); }
          break;
        case AM_KEY_ESCAPE:
          halt(0);
      }
    }
    if (ms() >= next) { // 每 150ms 走一步
      next += 150;
      step();
    }
  }
  return 0;
}
