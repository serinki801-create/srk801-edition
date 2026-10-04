// ============================================================================
// wm.c — Composite WM: оформление по Darling AppKit/CoreAnimation (Phase 6)
// ============================================================================
// Палитра — точные System-цвета Darling (X11Display.m, NSColor catalog):
//   mainMenuBarColor 0.90, controlColor 0.93, controlShadow 0.333,
//   selectedMenuItemColor blue(0,0,1), menuBackground 0.90, labelColor a0.847,
//   alternatingContentBackground (0.957,0.961,0.961), selectedContent
//   (0,0.388,0.882), controlHighlight 0.89, windowFrame lightGray 0.667.
// Меню — раскладка NSMainMenuView.m: height 22 (font13+8), padding 6px,
// hover/open = selectedMenuItemColor fill (NSGraphicsStyle.m), open — белый
// текст (selectedMenuItemTextColor), dropdown: menuBackground + 1px windowFrame.
// Окна: title bar controlColor, border 1px windowFrame, заголовок labelColor,
// светофоры FF5F57/FEBC2E/28C840 (Yosemite-палитра traffic lights).
// Dock: светлое Aqua-стекло, squircle-иконки; magnification и bounce — без
// изменений (анимации сохранены).
// ============================================================================
#include "wm.h"
#include "framebuffer.h"
#include "timer.h"
#include <stdint.h>

// --- Палитра Darling (System-цвета) --------------------------------------------
#define D_BAR_BG        0xE5E5E5 // mainMenuBarColor: white 0.90
#define D_BAR_BORDER    0xB9B9BE // нижняя граница menu bar
#define D_MENU_BG       0xE5E5E5 // menuBackgroundColor: white 0.90
#define D_CTRL          0xEDEDED // controlColor: white 0.93 (title bar)
#define D_CTRL_HI       0xFFFFFF // controlBackgroundColor
#define D_FRAME         0xB4B4B9 // рамка окна (windowFrameColor ~0.667)
#define D_LABEL         0x262628 // labelColor: black a0.847 по белому
#define D_LABEL_DIM     0x8E8E93 // неактивный заголовок
#define D_SELECT        0x0000FF // selectedMenuItemColor: blueColor (0,0,1)
#define D_SELECT_DIM    0x0063E1 // selectedContentBackgroundColor
#define D_TEXT          0x1A1A1A // menuItemTextColor ~black
#define D_TEXT_SEL      0xFFFFFF // selectedMenuItemTextColor
#define D_SEP           0xC4C4C8 // линии-разделители
#define D_ALT_ROW       0xF4F5F5 // alternatingContentBackgroundColor
#define D_TOOLTIP_BG    0xFFFFFF
#define D_TT_BORDER     0x8E8E93
#define D_DOCK_DOT      0x7A7A80 // running-app индикатор на светлом доке
#define D_TTL_RED       0xFF5F57
#define D_TTL_YEL       0xFEBC2E
#define D_TTL_GRN       0x28C840
#define D_TTL_RED_R     0xE1463F
#define D_TTL_YEL_R     0xD89E24
#define D_TTL_GRN_R     0x1FA832
#define D_TTL_INACT     0xC9C9CE
#define D_TTL_INACT_R   0xB4B4BB
#define D_SYM_RED       0x7A231E
#define D_SYM_YEL       0x8A6A14
#define D_SYM_GRN       0x1E6B2F

// --- Геометрия (NSMainMenuView.m / NSGraphicsStyle.m) ---------------------------
#define BAR_H           22       // menuHeight: font(13) + 8
#define TITLE_H         24       // title bar окна
#define WIN_RADIUS      6        // угол окна (эра 10.10)
#define DOCK_H          58
#define DOCK_RADIUS     20
#define TLT_R           6        // радиус светофора
#define MENU_ITEM_H     18
#define MENU_PAD        5        // вертикальный padding dropdown

#define WM_ANIM_STEPS   8
#define WM_ANIM_SLIDE   5
#define WM_BOUNCE_STEPS 6

typedef struct {
    int used, visible;
    int x, y, w, h;
    char title[WM_TITLE_MAX];
    char lines[WM_LINES][WM_LINE_MAX];
    int nlines;
    uint32_t bg;
    uint32_t fg;                 // цвет текста по умолчанию
    uint32_t line_fg[WM_LINES];  // per-line цвет (0 = не задан -> fg)
    char prefix[32];             // двухцветный prompt-префикс input-строки
    uint32_t prefix_fg;
    int prefix_line;             // индекс строки (или -1)
    int list_mode;               // Finder: штриховка + выбор строки
    int cursor_on;               // блок-курсор в конце input-строки
    int anim;
} wm_window_t;

static wm_window_t g_win[WM_MAX_WINDOWS];
static int g_focused = -1;
static uint32_t g_sw = 0, g_sh = 0;
static int g_mouse_x = 0, g_mouse_y = 0;
static uint8_t g_buttons = 0;
static int g_dock_sel = 0;
static char g_clock[32] = "up 0s";
static int g_drag = 0;
static int g_lowfx = 0;
static int g_bounce = 0;
static uint64_t g_last_tick = 0;
static int g_menu_open = -1;    // открытое top-меню (-1 = нет)
static int g_menu_hover = -1;   // hover-item в открынном меню
static int g_bar_hover = -1;    // hover top-меню (когда меню закрыто)
static int g_dock_hover = -1;   // hover иконки dock (tooltip)
static int g_running[WM_DOCK_APPS] = {0};
static wm_action_cb_t g_action_cb = 0;
static wm_list_cb_t g_list_cb = 0;

static const char *g_dock_names[WM_DOCK_APPS] = {
    "Finder", "Terminal", "Sys Info", "Run App", "Trash"
};

// --- Меню (разметка как в NSMainMenuView; action — реализация desktop.c) --------
typedef struct {
    const char *title;
    int action;
} wmi_t;

typedef struct {
    const char *title;
    wmi_t items[WM_MENU_ITEMS_MAX];
    int n;
} wm_menu_def_t;

static const wm_menu_def_t g_menus[WM_MENU_COUNT] = {
    { "File", {
        { "New Window", ACT_NEW_WINDOW },
        { "Open File...", ACT_NONE },
        { "Close Window", ACT_CLOSE },
        { "Print...", ACT_NONE },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 4 },
    { "Edit", {
        { "Undo", ACT_NONE },
        { "Redo", ACT_NONE },
        { "Clear", ACT_CLEAR },
        { "Select All", ACT_NONE },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 4 },
    { "View", {
        { "LowFX: Toggle", ACT_TOGGLE_FX },
        { "Enter Full Screen", ACT_ZOOM },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 2 },
    { "Go", {
        { "Home", ACT_ABOUT },
        { "Recents", ACT_NONE },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 2 },
    { "Window", {
        { "Minimize", ACT_MINIMIZE },
        { "Zoom", ACT_ZOOM },
        { "Close Window", ACT_CLOSE },
        { "Bring All to Front", ACT_NONE },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 4 },
    { "Help", {
        { "AI-OS Help", ACT_HELP },
        { "About AI-OS", ACT_ABOUT },
        { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
    }, 2 }
};

// --- Мелкие утилиты -------------------------------------------------------------
static void str_copy(char *dst, const char *src, int n)
{
    int i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int my_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int isqrt(int v)
{
    if (v <= 0)
        return 0;
    int r = v;
    int y = (r + 1) / 2;
    while (y < r) {
        r = y;
        y = (r + v / r) / 2;
    }
    return r;
}

static void fill_circle(int cx, int cy, int rad, uint32_t color)
{
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = isqrt(rad * rad - dy * dy);
        fb_fill_rect((uint32_t)(cx - dx), (uint32_t)(cy + dy),
                     (uint32_t)(dx * 2 + 1), 1, color);
    }
}

static int in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int in_circle(int px, int py, int cx, int cy, int rad)
{
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= rad * rad;
}

// Ширина текста AA-шрифтом (advance 9px).
static int aa_text_w(const char *s)
{
    return my_strlen(s) * 9 - 1;
}

// --- Раскладка menu bar ----------------------------------------------------------
// x прямоугольника-хита top-меню i (title ± 6px — как borderRect в NSMainMenuView).
#define APP_NAME "AI-OS"
#define APP_NAME_X 30

static int menubar_item_x(int i, int *w)
{
    int x = APP_NAME_X + aa_text_w(APP_NAME) + 14;
    for (int k = 0; k < i; k++)
        x += aa_text_w(g_menus[k].title) + 12 + 10; // padding±6 + межменю 10
    *w = aa_text_w(g_menus[i].title) + 12;
    return x;
}

// Dropdown-геометрия открытого меню.
static void dropdown_geom(int m, int *x, int *y, int *w, int *h)
{
    int hw;
    int mx = menubar_item_x(m, &hw);
    (void)hw;
    *x = mx - 6;
    *y = BAR_H;
    int maxw = 0;
    for (int k = 0; k < g_menus[m].n; k++) {
        int tw = aa_text_w(g_menus[m].items[k].title);
        if (tw > maxw)
            maxw = tw;
    }
    *w = 20 + maxw + 18; // gutter + текст + правый margin
    *h = MENU_PAD * 2 + g_menus[m].n * MENU_ITEM_H;
}

static int dropdown_item_at(int m, int x, int y)
{
    int dx, dy, dw, dh;
    dropdown_geom(m, &dx, &dy, &dw, &dh);
    if (!in_rect(x, y, dx + 1, dy + MENU_PAD, dw - 2, dh - MENU_PAD * 2))
        return -1;
    int row = (y - (dy + MENU_PAD)) / MENU_ITEM_H;
    if (row < 0 || row >= g_menus[m].n)
        return -1;
    return row;
}

// --- Иконки menu bar (чёрные, как в macOS) ---------------------------------------
static void draw_apple(int ax, int ay)
{
    fill_circle(ax, ay + 1, 5, D_TEXT);
    fb_fill_round_rect((uint32_t)(ax + 1), (uint32_t)(ay - 7), 4, 4, 2, D_TEXT);
    fill_circle(ax + 6, ay - 1, 4, D_BAR_BG); // "укус"
}

static void draw_wifi(int cx, int cy)
{
    for (int dy = -8; dy <= 0; dy++) {
        int dy2 = dy * dy;
        int limit = 3 * dy2;
        int limit_dx = isqrt(limit);
        if (limit_dx > 10) limit_dx = 10;
        for (int dx = -limit_dx; dx <= limit_dx; dx++) {
            int d2 = dx * dx + dy2;
            for (int ri = 0; ri < 3; ri++) {
                int r = 3 + ri * 2;
                int inner = (r - 1) * (r - 1);
                int outer = (r + 1) * (r + 1);
                if (d2 >= inner && d2 <= outer)
                    fb_put_pixel((uint32_t)(cx + dx), (uint32_t)(cy + dy), D_TEXT);
            }
        }
    }
    fb_fill_rect((uint32_t)(cx - 1), (uint32_t)(cy + 3), 2, 2, D_TEXT);
}

// Батарея: контур + заливка 80% + носик (outline-стиль macOS).
static void draw_battery(int bx, int by)
{
    fb_fill_round_rect((uint32_t)bx, (uint32_t)by, 22, 10, 2, D_TEXT);
    fb_fill_round_rect((uint32_t)(bx + 2), (uint32_t)(by + 2), 18, 6, 1, D_BAR_BG);
    fb_fill_round_rect((uint32_t)(bx + 2), (uint32_t)(by + 2), 14, 6, 1, D_TEXT);
    fb_fill_rect((uint32_t)(bx + 22), (uint32_t)(by + 3), 2, 4, D_TEXT);
}

// Control Center: две пилюли (toggle-стиль).
static void draw_cc(int cx, int cy)
{
    fb_fill_round_rect((uint32_t)cx, (uint32_t)cy, 14, 6, 3, D_TEXT);
    fb_fill_round_rect((uint32_t)cx, (uint32_t)(cy + 8), 14, 6, 3, D_TEXT);
}

// --- Публичный API ------------------------------------------------------------------
void wm_init(uint32_t screen_w, uint32_t screen_h)
{
    g_sw = screen_w;
    g_sh = screen_h;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        g_win[i].used = 0;
        g_win[i].visible = 0;
        g_win[i].anim = WM_ANIM_STEPS;
        g_win[i].cursor_on = 0;
        g_win[i].list_mode = 0;
        g_win[i].prefix_line = -1;
        for (int l = 0; l < WM_LINES; l++)
            g_win[i].line_fg[l] = 0;
    }
    g_focused = -1;
    g_dock_sel = 0;
    g_bounce = 0;
    g_last_tick = 0;
    g_menu_open = -1;
    g_menu_hover = -1;
    g_bar_hover = -1;
    g_dock_hover = -1;
    for (int i = 0; i < WM_DOCK_APPS; i++)
        g_running[i] = 0;
    g_clock[0] = 'u'; g_clock[1] = 'p'; g_clock[2] = ' ';
    g_clock[3] = '0'; g_clock[4] = 's'; g_clock[5] = '\0';
}

void wm_set_lowfx(int on) { g_lowfx = on ? 1 : 0; }
int wm_lowfx(void) { return g_lowfx; }

int wm_tick(void)
{
    uint64_t t = timer_ticks();
    if (t == g_last_tick || t == g_last_tick + 1)
        return 0;
    g_last_tick = t;
    int busy = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_win[i].used && g_win[i].visible && g_win[i].anim < WM_ANIM_STEPS) {
            g_win[i].anim++;
            busy = 1;
        }
    }
    if (g_bounce > 0) {
        g_bounce--;
        busy = 1;
    }
    return busy;
}

int wm_open(const char *title, int x, int y, int w, int h, uint32_t bg)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!g_win[i].used) {
            g_win[i].used = 1;
            g_win[i].visible = 1;
            g_win[i].x = x; g_win[i].y = y;
            g_win[i].w = w < 200 ? 200 : w;
            g_win[i].h = h < 100 ? 100 : h;
            str_copy(g_win[i].title, title ? title : "window", WM_TITLE_MAX);
            for (int l = 0; l < WM_LINES; l++) {
                g_win[i].lines[l][0] = '\0';
                g_win[i].line_fg[l] = 0;
            }
            g_win[i].nlines = 0;
            g_win[i].bg = bg;
            g_win[i].fg = D_LABEL;
            g_win[i].prefix[0] = '\0';
            g_win[i].prefix_line = -1;
            g_win[i].list_mode = 0;
            g_win[i].cursor_on = 0;
            g_win[i].anim = g_lowfx ? WM_ANIM_STEPS : 0;
            g_focused = i;
            return i;
        }
    }
    return -1;
}

void wm_close(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS)
        return;
    g_win[id].used = 0;
    g_win[id].visible = 0;
    if (g_focused == id) {
        g_focused = -1;
        wm_focus_next();
    }
}

void wm_set_line(int id, int idx, const char *text)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    if (idx < 0 || idx >= WM_LINES)
        return;
    str_copy(g_win[id].lines[idx], text ? text : "", WM_LINE_MAX);
    if (idx >= g_win[id].nlines)
        g_win[id].nlines = idx + 1;
}

void wm_clear_lines(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    for (int l = 0; l < WM_LINES; l++) {
        g_win[id].lines[l][0] = '\0';
        g_win[id].line_fg[l] = 0;
    }
    g_win[id].nlines = 0;
}

// Оформление контента.
void wm_set_fg(int id, uint32_t fg)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].fg = fg ? fg : D_LABEL;
}

void wm_set_line_fg(int id, int idx, uint32_t fg)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    if (idx < 0 || idx >= WM_LINES)
        return;
    g_win[id].line_fg[idx] = fg;
}

void wm_set_prefix(int id, int line, const char *text, uint32_t fg)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    str_copy(g_win[id].prefix, text ? text : "", sizeof(g_win[id].prefix));
    g_win[id].prefix_fg = fg;
    g_win[id].prefix_line = text && text[0] ? line : -1;
}

void wm_set_list_mode(int id, int on)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].list_mode = on ? 1 : 0;
}

void wm_set_cursor(int id, int on)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].cursor_on = on ? 1 : 0;
}

void wm_minimize(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].visible = 0;
    if (g_focused == id) {
        g_focused = -1;
        wm_focus_next();
    }
}

void wm_restore(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].visible = 1;
    g_win[id].anim = g_lowfx ? WM_ANIM_STEPS : 0;
    g_focused = id;
}

void wm_maximize(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_win[id].x = 0;
    g_win[id].y = BAR_H;
    g_win[id].w = (int)g_sw;
    g_win[id].h = (int)g_sh - BAR_H - DOCK_H - 4;
    if (g_win[id].h < 80)
        g_win[id].h = 80;
}

void wm_focus(int id)
{
    if (id < 0 || id >= WM_MAX_WINDOWS || !g_win[id].used)
        return;
    g_focused = id;
}

int wm_focused(void) { return g_focused; }

void wm_focus_next(void)
{
    for (int k = 1; k <= WM_MAX_WINDOWS; k++) {
        int id = (g_focused + k) % WM_MAX_WINDOWS;
        if (id < 0)
            id += WM_MAX_WINDOWS;
        if (g_win[id].used && g_win[id].visible) {
            g_focused = id;
            return;
        }
    }
}

void wm_move_focused(int dx, int dy)
{
    if (g_focused < 0 || !g_win[g_focused].used)
        return;
    g_win[g_focused].x += dx;
    g_win[g_focused].y += dy;
    if (g_win[g_focused].x < 0)
        g_win[g_focused].x = 0;
    if (g_win[g_focused].y < BAR_H + 2)
        g_win[g_focused].y = BAR_H + 2;
    if (g_win[g_focused].x + g_win[g_focused].w > (int)g_sw)
        g_win[g_focused].x = (int)g_sw - g_win[g_focused].w;
    if (g_win[g_focused].y + g_win[g_focused].h > (int)g_sh - 70)
        g_win[g_focused].y = (int)g_sh - 70 - g_win[g_focused].h;
}

void wm_dock_select(int idx)
{
    if (idx >= 0 && idx < WM_DOCK_APPS && idx != g_dock_sel) {
        g_dock_sel = idx;
        g_bounce = g_lowfx ? 0 : WM_BOUNCE_STEPS;
    }
}

int wm_dock_selected(void) { return g_dock_sel; }

void wm_set_clock(const char *s)
{
    str_copy(g_clock, s ? s : "", sizeof(g_clock));
}

int wm_menu_open(void) { return g_menu_open; }

void wm_menu_close(void)
{
    g_menu_open = -1;
    g_menu_hover = -1;
}

void wm_set_action_cb(wm_action_cb_t cb) { g_action_cb = cb; }
void wm_set_list_cb(wm_list_cb_t cb) { g_list_cb = cb; }

void wm_set_running(int idx, int on)
{
    if (idx < 0 || idx >= WM_DOCK_APPS)
        return;
    g_running[idx] = on ? 1 : 0;
}

static int g_drag_off_x = 0, g_drag_off_y = 0;

// --- Мышь -------------------------------------------------------------------------
void wm_mouse(int x, int y, uint8_t buttons)
{
    uint8_t prev = g_buttons;
    g_mouse_x = x;
    g_mouse_y = y;
    g_buttons = buttons;

    int clicked = (buttons & 1) && !(prev & 1);

    // Hover: top-меню и док.
    g_bar_hover = -1;
    if (y < BAR_H) {
        for (int i = 0; i < WM_MENU_COUNT; i++) {
            int w, mx = menubar_item_x(i, &w);
            if (x >= mx && x < mx + w) {
                g_bar_hover = i;
                break;
            }
        }
    }
    g_dock_hover = -1;
    {
        int dock_w = WM_DOCK_APPS * 56 + 16;
        int dock_x = ((int)g_sw - dock_w) / 2;
        int dock_y = (int)g_sh - 62;
        for (int k = 0; k < WM_DOCK_APPS; k++) {
            if (in_rect(x, y, dock_x + 8 + k * 56, dock_y + 2, 48, 54))
                g_dock_hover = k;
        }
    }

    // Hover-item внутри открытого dropdown.
    if (g_menu_open >= 0) {
        g_menu_hover = dropdown_item_at(g_menu_open, x, y);
        // Переход мышью по другому top-меню переключает открытое (macOS-логика).
        if (g_bar_hover >= 0 && g_bar_hover != g_menu_open)
            g_menu_open = g_bar_hover;
    }

    // Drag за title bar — до всего остального.
    if ((buttons & 1) && g_drag && g_focused >= 0 && g_win[g_focused].used) {
        g_win[g_focused].x = x - g_drag_off_x;
        g_win[g_focused].y = y - g_drag_off_y;
        if (g_win[g_focused].x < 0)
            g_win[g_focused].x = 0;
        if (g_win[g_focused].y < BAR_H + 2)
            g_win[g_focused].y = BAR_H + 2;
        return;
    }
    if (!(buttons & 1))
        g_drag = 0;
    if (!clicked)
        return;

    // Клик в menu bar: открыть/закрыть меню.
    if (y < BAR_H) {
        if (g_bar_hover >= 0) {
            if (g_menu_open == g_bar_hover)
                g_menu_open = -1;
            else {
                g_menu_open = g_bar_hover;
                g_menu_hover = -1;
            }
        } else if (g_menu_open >= 0) {
            g_menu_open = -1;
        }
        return;
    }

    // Клик по пункту открытого dropdown.
    if (g_menu_open >= 0) {
        int m = g_menu_open;
        int row = dropdown_item_at(m, x, y);
        g_menu_open = -1;
        g_menu_hover = -1;
        if (row >= 0 && g_action_cb)
            g_action_cb(g_menus[m].items[row].action);
        return;
    }

    // Клик по окну: close-кнопка / drag / фокус / строки списка.
    if (g_focused >= 0 && g_win[g_focused].used && g_win[g_focused].visible) {
        wm_window_t *w = &g_win[g_focused];
        if (in_circle(x, y, w->x + 14, w->y + 12, 8)) {
            wm_close(g_focused);
            return;
        }
        if (in_rect(x, y, w->x, w->y, w->w, TITLE_H)) {
            g_drag = 1;
            g_drag_off_x = x - w->x;
            g_drag_off_y = y - w->y;
            return;
        }
        if (in_rect(x, y, w->x, w->y, w->w, w->h)) {
            if (w->list_mode && g_list_cb) {
                int body_top = w->y + TITLE_H + 10;
                int row = (y - body_top) / 11;
                if (row >= 0 && row < w->nlines)
                    g_list_cb(g_focused, row);
                else
                    g_list_cb(g_focused, -1);
            }
            return;
        }
    }
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (i == g_focused || !g_win[i].used || !g_win[i].visible)
            continue;
        wm_window_t *w = &g_win[i];
        if (in_rect(x, y, w->x, w->y, w->w, w->h)) {
            g_focused = i;
            if (in_rect(x, y, w->x, w->y, w->w, TITLE_H)) {
                g_drag = 1;
                g_drag_off_x = x - w->x;
                g_drag_off_y = y - w->y;
            }
            return;
        }
    }
    {
        int dock_w = WM_DOCK_APPS * 56 + 16;
        int dock_x = ((int)g_sw - dock_w) / 2;
        int dock_y = (int)g_sh - 62;
        for (int k = 0; k < WM_DOCK_APPS; k++) {
            if (in_rect(x, y, dock_x + 8 + k * 56, dock_y + 6, 48, 48)) {
                if (k != g_dock_sel) {
                    g_dock_sel = k;
                    g_bounce = g_lowfx ? 0 : WM_BOUNCE_STEPS;
                }
                return;
            }
        }
    }
}

// --- Композит ----------------------------------------------------------------------
// Обои: вертикальный градиент (Mojave-стандарт, сохраняется).
static void draw_desktop(void)
{
    uint32_t stops[][2] = {
        {0,   0x0A0F2E},
        {64,  0x251D55},
        {163, 0x5C3160},
        {199, 0xD06A3E},
        {230, 0x7A3A35},
        {256, 0x1E1630}
    };
    int nstops = 6;
    for (uint32_t y = 0; y < g_sh; y++) {
        uint64_t t = ((uint64_t)y * 256) / (g_sh ? g_sh : 1);
        uint32_t color = stops[0][1];
        for (int i = 1; i < nstops; i++) {
            if (t <= stops[i][0]) {
                uint64_t k = (t - stops[i - 1][0]) * 256 / (stops[i][0] - stops[i - 1][0]);
                uint32_t c0 = stops[i - 1][1], c1 = stops[i][1];
                uint32_t r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
                uint32_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
                uint32_t r = (r0 * (256 - k) + r1 * k) >> 8;
                uint32_t g = (g0 * (256 - k) + g1 * k) >> 8;
                uint32_t b = (b0 * (256 - k) + b1 * k) >> 8;
                color = (r << 16) | (g << 8) | b;
                break;
            }
        }
        fb_fill_rect(0, y, g_sw, 1, color);
    }
    // Силуэты гор (Mojave)
    fill_circle((int)(g_sw / 4), (int)g_sh + 50, (int)(g_sh * 7 / 25), 0x14102A);
    fill_circle((int)(g_sw * 3 / 4), (int)g_sh + 90, (int)(g_sh * 8 / 25), 0x0F0C20);
}

// Мягкая многослойная тень (macOS-объём).
static void draw_shadow(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (g_lowfx) {
        fb_fill_rect((uint32_t)(x + 3), (uint32_t)(y + 4),
                     (uint32_t)w, (uint32_t)h, 0x000000);
        return;
    }
    fb_fill_round_rect_alpha((uint32_t)(x + 2), (uint32_t)(y + 4),
                             (uint32_t)w, (uint32_t)h, WIN_RADIUS + 4, 0x000000, 40);
    fb_fill_round_rect_alpha((uint32_t)(x + 5), (uint32_t)(y + 7),
                             (uint32_t)w, (uint32_t)h, WIN_RADIUS + 3, 0x000000, 28);
    fb_fill_round_rect_alpha((uint32_t)(x + 8), (uint32_t)(y + 10),
                             (uint32_t)w, (uint32_t)h, WIN_RADIUS + 2, 0x000000, 16);
}

static void draw_traffic_light_symbol(int lx, int ly, int type)
{
    uint32_t sc = (type == 0) ? D_SYM_RED : (type == 1) ? D_SYM_YEL : D_SYM_GRN;
    if (type == 0) { // close X
        fb_fill_rect((uint32_t)(lx - 2), (uint32_t)(ly - 2), 1, 5, sc);
        fb_fill_rect((uint32_t)(lx + 2), (uint32_t)(ly - 2), 1, 5, sc);
        fb_fill_rect((uint32_t)(lx - 1), (uint32_t)(ly - 1), 1, 3, sc);
        fb_fill_rect((uint32_t)(lx + 1), (uint32_t)(ly - 1), 1, 3, sc);
        fb_fill_rect((uint32_t)(lx),     (uint32_t)(ly),     1, 1, sc);
    } else if (type == 1) { // minimize -
        fb_fill_rect((uint32_t)(lx - 2), (uint32_t)(ly), 5, 1, sc);
    } else { // zoom +
        fb_fill_rect((uint32_t)(lx - 2), (uint32_t)(ly - 1), 5, 1, sc);
        fb_fill_rect((uint32_t)(lx),     (uint32_t)(ly - 2), 1, 5, sc);
    }
}

static void draw_window(wm_window_t *w, int focused)
{
    if (!w->used || !w->visible)
        return;
    int yoff = 0;
    if (!g_lowfx && w->anim < WM_ANIM_STEPS)
        yoff = (WM_ANIM_STEPS - w->anim) * WM_ANIM_SLIDE;
    int x = w->x, y = w->y + yoff;

    draw_shadow(x, y, w->w, w->h);
    if (g_lowfx) {
        fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w->w, (uint32_t)w->h,
                     D_FRAME);
        fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                     (uint32_t)(w->w - 2), (uint32_t)(w->h - 2), w->bg);
    } else {
        fb_fill_round_rect((uint32_t)x, (uint32_t)y, (uint32_t)w->w,
                           (uint32_t)w->h, WIN_RADIUS, D_FRAME);
        fb_fill_round_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                           (uint32_t)(w->w - 2), (uint32_t)(w->h - 2),
                           WIN_RADIUS - 1, w->bg);
    }

    // Title bar: controlColor 0.93 (округлённый верх, прямые нижние углы).
    if (g_lowfx) {
        fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                     (uint32_t)(w->w - 2), TITLE_H, D_CTRL);
    } else {
        fb_fill_round_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                           (uint32_t)(w->w - 2), TITLE_H + 4, WIN_RADIUS - 1, D_CTRL);
        fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1 + 6),
                     (uint32_t)(w->w - 2), TITLE_H - 6, D_CTRL);
        // верхний блик + линия под title bar (NSGraphicsStyle: 1px border).
        fb_fill_rect_alpha((uint32_t)(x + 9), (uint32_t)(y + 2),
                           (uint32_t)(w->w - 18), 1, 0xFFFFFF, 100);
    }
    fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + TITLE_H),
                 (uint32_t)(w->w - 2), 1, D_SEP);

    // Светофоры (Yosemite-палитра; hover — символы, как в macOS).
    int lx[3] = {x + 14, x + 32, x + 50};
    int ly = y + 12;
    for (int i = 0; i < 3; i++) {
        uint32_t ring, fill;
        if (focused) {
            ring = (i == 0) ? D_TTL_RED_R : (i == 1) ? D_TTL_YEL_R : D_TTL_GRN_R;
            fill = (i == 0) ? D_TTL_RED : (i == 1) ? D_TTL_YEL : D_TTL_GRN;
        } else {
            ring = D_TTL_INACT_R;
            fill = D_TTL_INACT;
        }
        if (g_lowfx) {
            fill_circle(lx[i], ly, TLT_R - 1, fill);
        } else {
            fill_circle(lx[i], ly, TLT_R, ring);
            fill_circle(lx[i], ly, TLT_R - 1, fill);
            if (focused) {
                int dx = g_mouse_x - lx[i], dy = g_mouse_y - ly;
                if (dx * dx + dy * dy <= TLT_R * TLT_R)
                    draw_traffic_light_symbol(lx[i], ly, i);
            }
        }
    }

    // Заголовок: labelColor, по центру (AA bold — как semibold 13pt).
    int tlen = my_strlen(w->title);
    int tw = tlen * 9;
    int tx = x + (w->w - tw) / 2;
    if (tx < x + 62) tx = x + 66;
    if (focused)
        fb_draw_string_aa_bold(w->title, (uint32_t)tx, (uint32_t)(y + 8), D_LABEL);
    else
        fb_draw_string_aa(w->title, (uint32_t)tx, (uint32_t)(y + 8), D_LABEL_DIM);

    // Тело окна: строки (персональный цвет строки + prompt-префикс + курсор).
    int body_top = y + TITLE_H + 10;
    int icon_w = w->list_mode ? 12 : 0;
    for (int l = 0; l < w->nlines && l < WM_LINES; l++) {
        if (w->lines[l][0] == '\0')
            continue;
        int row_y = body_top + l * 11;
        if (row_y + 10 > y + w->h)
            break;
        // List mode: штриховка (alternatingContentBackground) и selection.
        int sel = 0;
        if (w->list_mode && !g_lowfx) {
            if (l % 2 == 1)
                fb_fill_rect((uint32_t)(x + 2), (uint32_t)(row_y - 3),
                             (uint32_t)(w->w - 4), 11, D_ALT_ROW);
            sel = in_rect(g_mouse_x, g_mouse_y, x + 2, row_y - 3, w->w - 4, 11)
                  && focused;
            if (sel)
                fb_fill_rect((uint32_t)(x + 2), (uint32_t)(row_y - 3),
                             (uint32_t)(w->w - 4), 11, D_SELECT_DIM);
        }
        uint32_t lfg = sel ? D_CTRL_HI : (w->line_fg[l] ? w->line_fg[l] : w->fg);
        int tx0 = x + 10 + icon_w;
        if (w->list_mode && w->line_fg[l]) {
            // Маркер строки (папка/файл): маленький скруглённый квадрат.
            fb_fill_round_rect((uint32_t)(x + 8), (uint32_t)(row_y + 1), 7, 7, 2,
                               sel ? D_CTRL_HI : w->line_fg[l]);
        }
        if (w->prefix_line == l && w->prefix[0]) {
            int pw = aa_text_w(w->prefix);
            fb_draw_string_aa(w->prefix, (uint32_t)tx0, (uint32_t)row_y, w->prefix_fg);
            fb_draw_string_aa(w->lines[l], (uint32_t)(tx0 + pw + 9),
                              (uint32_t)row_y, lfg);
            // Блок-курсор после input.
            if (w->cursor_on && focused) {
                int cl = my_strlen(w->lines[l]);
                int cx0 = tx0 + pw + 9 + cl * 9;
                fb_fill_rect((uint32_t)cx0, (uint32_t)(row_y - 1), 7, 10, D_CTRL_HI);
            }
        } else {
            fb_draw_string_aa(w->lines[l], (uint32_t)tx0, (uint32_t)row_y, lfg);
        }
    }
}

// Menu bar: mainMenuBarColor 0.90, пункты black, hover/open — blue fill
// (NSGraphicsStyle drawMenuBarItemBorderInRect), open — белый текст.
static void draw_menubar(void)
{
    fb_fill_rect(0, 0, g_sw, BAR_H, D_BAR_BG);
    fb_fill_rect(0, BAR_H - 1, g_sw, 1, D_BAR_BORDER);

    draw_apple(14, 12);
    fb_draw_string_aa_bold(APP_NAME, APP_NAME_X, 7, D_TEXT);

    for (int i = 0; i < WM_MENU_COUNT; i++) {
        int w, mx = menubar_item_x(i, &w);
        if (mx + w + 120 > (int)g_sw)
            break;
        int open = (g_menu_open == i);
        int hover = (g_bar_hover == i);
        if (!g_lowfx && (open || hover)) {
            fb_fill_rect((uint32_t)mx, 2, (uint32_t)w, BAR_H - 4, D_SELECT);
        }
        uint32_t tc = open ? D_TEXT_SEL : D_TEXT;
        fb_draw_string_aa(g_menus[i].title, (uint32_t)mx + 6, 7, tc);
    }

    // Статус-иконки справа: часы -> Control Center -> WiFi -> батарея (black).
    int rx = (int)g_sw;
    int cw = aa_text_w(g_clock);
    rx -= cw + 12;
    if (rx > 10)
        fb_draw_string_aa(g_clock, (uint32_t)rx, 7, D_TEXT);

    rx -= 28;
    if (rx > 10)
        draw_cc(rx, 8);

    rx -= 32;
    if (rx > 10)
        draw_wifi(rx + 14, 13);

    rx -= 40;
    if (rx > 10)
        draw_battery(rx, 7);
}

// Dropdown-меню (NSMenuView): menuBackground 0.90 + 1px windowFrame border,
// hover-строка = selectedMenuItemColor + белый текст.
static void draw_dropdown(void)
{
    if (g_menu_open < 0)
        return;
    int dx, dy, dw, dh;
    dropdown_geom(g_menu_open, &dx, &dy, &dw, &dh);
    if (!g_lowfx) {
        fb_fill_round_rect_alpha((uint32_t)(dx + 2), (uint32_t)(dy + 3),
                                 (uint32_t)dw, (uint32_t)dh, 6, 0x000000, 40);
        fb_fill_round_rect_alpha((uint32_t)(dx + 5), (uint32_t)(dy + 6),
                                 (uint32_t)dw, (uint32_t)dh, 5, 0x000000, 20);
    }
    if (g_lowfx) {
        fb_fill_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)dw, (uint32_t)dh,
                     D_BAR_BORDER);
        fb_fill_rect((uint32_t)(dx + 1), (uint32_t)(dy + 1),
                     (uint32_t)(dw - 2), (uint32_t)(dh - 2), D_MENU_BG);
    } else {
        fb_fill_round_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)dw, (uint32_t)dh,
                           4, D_BAR_BORDER);
        fb_fill_round_rect((uint32_t)(dx + 1), (uint32_t)(dy + 1),
                           (uint32_t)(dw - 2), (uint32_t)(dh - 2), 3, D_MENU_BG);
    }
    for (int k = 0; k < g_menus[g_menu_open].n; k++) {
        int iy = dy + MENU_PAD + k * MENU_ITEM_H;
        if (g_menu_hover == k) {
            fb_fill_rect((uint32_t)(dx + 1), (uint32_t)iy,
                         (uint32_t)(dw - 2), MENU_ITEM_H, D_SELECT);
        }
        uint32_t tc = (g_menu_hover == k) ? D_TEXT_SEL : D_TEXT;
        fb_draw_string_aa(g_menus[g_menu_open].items[k].title,
                          (uint32_t)(dx + 20), (uint32_t)(iy + 5), tc);
    }
}

// Dock: светлое Aqua-стекло (10.10), squircle-иконки, tooltip, running-точки.
static void draw_dock_icon(int k, int dx, int dy, int sz)
{
    int r = (sz * 22) / 100;
    switch (k) {
    case 0: { // Finder: белая плитка + синяя правая половина + "лицо".
        fb_fill_round_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)sz, (uint32_t)sz,
                           r, 0xFFFFFF);
        fb_fill_round_rect((uint32_t)(dx + sz / 2 - 1), (uint32_t)(dy + 1),
                           (uint32_t)(sz / 2 + 1), (uint32_t)(sz - 2), r, 0x2F8FE8);
        int ey = dy + sz * 3 / 10;
        int eh = sz / 8;
        fb_fill_rect((uint32_t)(dx + sz * 2 / 8), (uint32_t)ey, 2, (uint32_t)eh, 0x222222);
        fb_fill_rect((uint32_t)(dx + sz * 5 / 8), (uint32_t)ey, 2, (uint32_t)eh, 0xFFFFFF);
        int sy = dy + sz * 4 / 10 + 2;
        for (int t = -3; t <= 3; t++) {
            int off = (t * t) / 3;
            fb_fill_rect((uint32_t)(dx + sz / 2 - 3 + t - 1), (uint32_t)(sy + off),
                         2, 1, 0x222222);
            fb_fill_rect((uint32_t)(dx + sz / 2 + 3 + t - 1), (uint32_t)(sy + off),
                         2, 1, 0xFFFFFF);
        }
        break;
    }
    case 1: { // Terminal: тёмная плитка + ">_"
        fb_fill_round_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)sz, (uint32_t)sz,
                           r, 0x2B2B2D);
        fb_draw_string_aa_bold(">_", (uint32_t)(dx + (sz - 17) / 2),
                               (uint32_t)(dy + (sz - 8) / 2 - 1), 0xFFFFFF);
        break;
    }
    case 2: { // Sys Info: светло-серая плитка + "i"
        fb_fill_round_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)sz, (uint32_t)sz,
                           r, 0xB8B8C0);
        fb_fill_rect((uint32_t)(dx + sz / 2 - 1), (uint32_t)(dy + sz * 2 / 10), 2, 2,
                     0x3A3A40);
        fb_fill_rect((uint32_t)(dx + sz / 2 - 1), (uint32_t)(dy + sz * 4 / 10), 2,
                     (uint32_t)(sz * 5 / 10), 0x3A3A40);
        break;
    }
    case 3: { // Run App: зелёная плитка + play-треугольник
        fb_fill_round_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)sz, (uint32_t)sz,
                           r, 0x30C24E);
        int ty = dy + sz / 2 - 5;
        for (int i = 0; i < 10; i++)
            fb_fill_rect((uint32_t)(dx + sz / 2 - 5), (uint32_t)(ty + i),
                         (uint32_t)(10 - i), 1, 0xFFFFFF);
        break;
    }
    default: { // Trash: металлический ящик (без плитки).
        int lid_w = sz * 5 / 8;
        int bx = dx + (sz - lid_w) / 2;
        fb_fill_round_rect((uint32_t)bx, (uint32_t)(dy + sz / 8), (uint32_t)lid_w, 4, 2,
                           0xA8A8B0);
        fb_fill_rect((uint32_t)(bx + lid_w / 2 - 2), (uint32_t)(dy + sz / 8 - 3), 4, 3,
                     0xA8A8B0);
        // Корпус: трапеция (сужается книзу) + рёбра.
        int top_y = dy + sz / 8 + 5;
        int bot_y = dy + sz - sz / 10;
        for (int yy = top_y; yy < bot_y; yy++) {
            int t = (yy - top_y) * 4 / (bot_y - top_y);
            int wrow = lid_w - t;
            int rx0 = bx + (lid_w - wrow) / 2;
            fb_fill_rect((uint32_t)rx0, (uint32_t)yy, (uint32_t)wrow, 1, 0xC0C0C6);
        }
        for (int i = 1; i < 4; i++) {
            int xx = bx + lid_w * i / 4 - 1;
            fb_fill_rect((uint32_t)xx, (uint32_t)top_y, 1,
                         (uint32_t)(bot_y - top_y), 0x9A9AA0);
        }
    }
    }
}

static void draw_dock(void)
{
    int dock_w = WM_DOCK_APPS * 56 + 16;
    int dock_x = ((int)g_sw - dock_w) / 2;
    int dock_y = (int)g_sh - 62;
    if (dock_x < 0)
        dock_x = 0;

    if (g_lowfx) {
        fb_fill_rect((uint32_t)dock_x, (uint32_t)dock_y, (uint32_t)dock_w, 58,
                     0xDADADA);
    } else {
        // Мягкая тень под доком.
        fb_fill_round_rect_alpha((uint32_t)(dock_x + 2), (uint32_t)(dock_y + 3),
                                 (uint32_t)dock_w, 58, DOCK_RADIUS + 2, 0x000000, 35);
        fb_fill_round_rect_alpha((uint32_t)(dock_x + 5), (uint32_t)(dock_y + 6),
                                 (uint32_t)dock_w, 58, DOCK_RADIUS + 1, 0x000000, 18);
        // Граница + светлое стекло (Aqua).
        fb_fill_round_rect((uint32_t)dock_x, (uint32_t)dock_y, (uint32_t)dock_w, 58,
                           DOCK_RADIUS, 0x9E9EA4);
        fb_fill_round_rect((uint32_t)(dock_x + 1), (uint32_t)(dock_y + 1),
                           (uint32_t)(dock_w - 2), 56, DOCK_RADIUS - 1, D_CTRL);
        fb_fill_round_rect_alpha((uint32_t)(dock_x + 2), (uint32_t)(dock_y + 2),
                                 (uint32_t)(dock_w - 4), 54, DOCK_RADIUS - 2,
                                 0xFFFFFF, 120);
        // Верхний блик.
        fb_fill_rect_alpha((uint32_t)(dock_x + 14), (uint32_t)(dock_y + 2),
                           (uint32_t)(dock_w - 28), 1, 0xFFFFFF, 90);
    }

    for (int k = 0; k < WM_DOCK_APPS; k++) {
        int ix = dock_x + 8 + k * 56;
        int iy = dock_y + 5;
        int sel = (k == g_dock_sel);
        int lift = 0;
        if (sel && g_bounce > 0 && !g_lowfx)
            lift = (g_bounce * g_bounce) / 12;
        int mag = 0;
        if (!g_lowfx) {
            int cx = ix + 24;
            int d = g_mouse_x > cx ? g_mouse_x - cx : cx - g_mouse_x;
            if (d < 120)
                mag = (120 - d) / 12;
        }
        int base = sel ? 48 : 40;
        int sz = base + mag;
        if (sz > 56)
            sz = 56;
        int pad = (48 - sz) / 2;
        int dx = ix + pad, dy = iy + pad - lift - mag / 2;
        draw_dock_icon(k, dx, dy, sz);
        // Точка запущенного приложения (macOS: dot под иконкой).
        if (g_running[k])
            fb_fill_rect((uint32_t)(ix + 22), (uint32_t)(dock_y + 55), 4, 2,
                         g_lowfx ? 0x555555 : D_DOCK_DOT);
    }
    // Tooltip под курсором (macOS-style).
    if (!g_lowfx && g_dock_hover >= 0) {
        const char *nm = g_dock_names[g_dock_hover];
        int nw = aa_text_w(nm);
        int bw = nw + 14;
        int bx = dock_x + 8 + g_dock_hover * 56 + 24 - bw / 2;
        int by = dock_y - 22;
        if (bx < 2) bx = 2;
        if (bx + bw > (int)g_sw - 2) bx = (int)g_sw - 2 - bw;
        fb_fill_round_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, 16, 4, D_TT_BORDER);
        fb_fill_round_rect((uint32_t)(bx + 1), (uint32_t)(by + 1),
                           (uint32_t)(bw - 2), 14, 3, D_TOOLTIP_BG);
        fb_draw_string_aa(nm, (uint32_t)(bx + 7), (uint32_t)(by + 4), D_TEXT);
    }
}

// Курсор macOS: белая стрелка с чёрным контуром.
static void draw_cursor(void)
{
    int x = g_mouse_x, y = g_mouse_y;
    static const uint8_t widths[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12};
    // Чёрный контур: расширение каждой строки на 1px (8-соседей достаточно).
    for (int row = 0; row < 13; row++) {
        uint32_t w = widths[row];
        fb_fill_rect((uint32_t)(x - 1), (uint32_t)(y + row - 1),
                     w + 2, 3, 0x000000);
        // "ноги" стрелки (rows 13-15)
    }
    // Ноги: две полосы с зазором.
    for (int row = 13; row <= 15; row++) {
        int lw = (row == 15) ? 4 : 5;
        int rw = (row == 15) ? 3 : 4;
        fb_fill_rect((uint32_t)(x - 1), (uint32_t)(y + row - 1),
                     (uint32_t)(lw + 1), 3, 0x000000);
        fb_fill_rect((uint32_t)(x + 7 - 1), (uint32_t)(y + row - 1),
                     (uint32_t)(rw + 1), 3, 0x000000);
    }
    // Белая заливка.
    for (int row = 0; row < 13; row++)
        fb_fill_rect((uint32_t)x, (uint32_t)(y + row), widths[row], 1, 0xFFFFFF);
    for (int row = 13; row <= 15; row++) {
        int lw = (row == 15) ? 4 : 5;
        int rw = (row == 15) ? 3 : 4;
        fb_fill_rect((uint32_t)x, (uint32_t)(y + row), lw, 1, 0xFFFFFF);
        fb_fill_rect((uint32_t)(x + 7), (uint32_t)(y + row), rw, 1, 0xFFFFFF);
    }
}

void wm_render(void)
{
    if (g_sw == 0 || g_sh == 0)
        return;
    draw_desktop();
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (i == g_focused)
            continue;
        draw_window(&g_win[i], 0);
    }
    if (g_focused >= 0)
        draw_window(&g_win[g_focused], 1);
    draw_menubar();
    draw_dropdown();
    draw_dock();
    draw_cursor();
    fb_flip();
}
