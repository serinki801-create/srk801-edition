// ============================================================================
// wm.h — Composite WM: macOS/Darling AppKit-оформление (Phase 6)
// ============================================================================
// Визуал переведён на реальные значения Darling (darwin AppKit/CoreAnimation):
// палитра System-цветов из X11Display.m (mainMenuBarColor, controlColor,
// selectedMenuItemColor...), отрисовка меню — NSGraphicsStyle.m/NSMainMenuView.m,
// заголовки окон — controlColor 0.93 + labelColor. Анимации (slide, bounce,
// magnification) и lowfx — сохранены.
// ============================================================================
#ifndef WM_H
#define WM_H

#include <stdint.h>

#define WM_MAX_WINDOWS 6
#define WM_TITLE_MAX 48
#define WM_LINES 10
#define WM_LINE_MAX 64
#define WM_DOCK_APPS 5
#define WM_MENU_COUNT 6
#define WM_MENU_ITEMS_MAX 8

// Действия пунктов меню (реализуются колбэком desktop.c).
typedef enum {
    ACT_NONE = 0,
    ACT_NEW_WINDOW,
    ACT_CLOSE,
    ACT_MINIMIZE,
    ACT_ZOOM,
    ACT_CLEAR,
    ACT_ABOUT,
    ACT_HELP,
    ACT_TOGGLE_FX,
    ACT_QUIT
} wm_action_t;

// Клик по строке в окне list_mode (Finder): cb(id, row) или cb(id, -1).
typedef int (*wm_list_cb_t)(int win_id, int row);
typedef void (*wm_action_cb_t)(int action);

void wm_init(uint32_t screen_w, uint32_t screen_h);

// Открыть окно, возвращает id (0..WM_MAX_WINDOWS-1) или -1.
int wm_open(const char *title, int x, int y, int w, int h, uint32_t bg);
void wm_close(int id);
void wm_set_line(int id, int idx, const char *text);
void wm_clear_lines(int id);

// Оформление контента: цвет текста окна и строк, двухцветная строка
// (prompt-префикс), режим списка (Finder: штриховка + выбор мышью).
void wm_set_fg(int id, uint32_t fg);
void wm_set_line_fg(int id, int idx, uint32_t fg);
void wm_set_prefix(int id, int line, const char *text, uint32_t fg);
void wm_set_list_mode(int id, int on);
void wm_set_cursor(int id, int on); // блок-курсор в конце input-строки

// Окна: скрыть/показать (Minimize), maximise (Zoom).
void wm_minimize(int id);
void wm_restore(int id);
void wm_maximize(int id);

// Фокус и перемещение.
void wm_focus(int id);
int wm_focused(void);
void wm_focus_next(void);
void wm_move_focused(int dx, int dy);

// Dock: выбор иконки (0..WM_DOCK_APPS-1), -1 — снять выбор.
void wm_dock_select(int idx);
int wm_dock_selected(void);
// Точка "запущено" под иконкой.
void wm_set_running(int idx, int on);

// Мышь (координаты уже в пикселях экрана).
void wm_mouse(int x, int y, uint8_t buttons);

// Меню: -1 = закрыто.
int wm_menu_open(void);
void wm_menu_close(void);
void wm_set_action_cb(wm_action_cb_t cb);
void wm_set_list_cb(wm_list_cb_t cb);

// Полный композит кадра + flip.
void wm_render(void);

// Анимации: продвинуть на шаг (троттлинг ~50fps по PIT).
// Возвращает 1, если что-то двигалось и нужен перерендер.
int wm_tick(void);

// LowFX-режим для слабого железа: без стекла/теней/анимаций.
void wm_set_lowfx(int on);
int wm_lowfx(void);

// Текст часов для menu bar (обновляет desktop).
void wm_set_clock(const char *s);

#endif
