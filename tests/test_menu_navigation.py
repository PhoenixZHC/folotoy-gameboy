"""执行生产游戏列表与入口路由，验证入页持键不会启动游戏。"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/game_main.c').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]
functions = '\n'.join(function(s) for s in (
    'static gb_menu_action_t board_menu_action(',
    'static BaseType_t menu_button_receive(',
    'static void menu_loop(void)',
    'static void input_entry_loop(void)',
))
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gb_input.h"
enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK, BSP_BTN_CLICK, BSP_BTN_LONG };
typedef int BaseType_t;
typedef int TickType_t;
typedef struct { int button, event; } button_message_t;
typedef struct { char name[32]; } gb_rom_entry_t;
typedef struct { pad_sample_t pad; int board; } event_t;
#define GB_UI_MAX_ITEMS 8
#define pdTRUE 1
static int s_buttons, frame, count, resets, launched, first_launch, drawn, pairs;
static size_t selected_game, games;
static bool pair_success;
static pad_sample_t initial;
static event_t events[64];
static pad_sample_t gamepad_snapshot(void) { return frame < 0 ? initial : events[frame].pad; }
static int xQueueReceive(int queue, button_message_t *msg, int timeout) {
    (void)queue; assert((timeout == 0 || timeout == 50) && ++frame < count);
    int b=events[frame].board;
    if (b < 0) return 0;
    *msg=(button_message_t){b==10?BSP_BTN_OK:b,b==10?BSP_BTN_LONG:BSP_BTN_CLICK};
    return pdTRUE;
}
static void xQueueReset(int queue) { (void)queue; ++resets; }
static int64_t esp_timer_get_time(void) { return (frame+1)*50000; }
static size_t gb_storage_count(void) { return games; }
static const gb_rom_entry_t *gb_storage_entry(size_t i) {
    static const gb_rom_entry_t rom={"test"}; assert(i<games); return &rom;
}
static void run_game(size_t i) {
    if (!launched) first_launch=frame;
    ++launched; selected_game=i;
}
static bool pairing_loop(void) {
    ++pairs; if (pair_success) initial.connected=true; return pair_success;
}
static void vTaskDelay(int ticks) { assert(ticks==20); }
#define pdMS_TO_TICKS(n) (n)
static void show_ui(const char *title,const char *hint,const char **items,
                    size_t n,size_t selected,const char *footer) {
    (void)hint; (void)footer;
    assert(!strcmp(title,"游戏列表") && n && selected<n && items[0]);
    ++drawn;
}
'''
suffix = r'''
static void reset(pad_sample_t pad) {
    initial=pad;frame=-1;count=resets=launched=drawn=pairs=0;
    first_launch=-1;selected_game=0;games=3;pair_success=false;
}
static void add(pad_sample_t pad,int board) { events[count++]=(event_t){pad,board}; }
int main(void) {
    pad_sample_t neutral={.connected=true}, held={.connected=true,.a=true},
                 down={.connected=true,.down=true};
    // 主页的 A 仍按住时进入列表：必须先松开，不能自动启动第一个游戏。
    reset(held);add(held,-1);add(neutral,-1);add(neutral,10);
    menu_loop(); assert(!launched && drawn && resets==1);
    // 松开后重新按 A 才启动；从游戏返回时仍按住 A 不重复启动。
    reset(held);add(held,-1);add(neutral,-1);add(held,-1);add(held,-1);
    add(neutral,-1);add(neutral,10);
    menu_loop(); assert(launched==1 && first_launch==2 && selected_game==0 && resets==2);
    // 入页已按住方向不跳行，释放后新按键正常选择。
    reset(down);add(down,-1);add(neutral,-1);add(down,-1);add(neutral,-1);
    add(held,-1);add(neutral,10);
    menu_loop(); assert(launched==1 && selected_game==1);
    // 断连直接回主页，已连接的入口不重新进入配对。
    reset(neutral);add(neutral,-1);add((pad_sample_t){0},-1);
    input_entry_loop(); assert(!pairs && !launched && drawn);
    // 未连接先配对；取消不进入列表，成功后仍只展示列表。
    reset((pad_sample_t){0});input_entry_loop();assert(pairs==1 && !drawn && !launched);
    reset((pad_sample_t){0});pair_success=true;add(neutral,-1);add(neutral,10);
    input_entry_loop();assert(pairs==1 && drawn && !launched);
    // 原有机身确认仍可在列表内启动选中游戏。
    reset(neutral);add(neutral,BSP_BTN_OK);add(neutral,10);
    menu_loop();assert(launched==1 && selected_game==0);
    // 空列表断连也返回；无游戏时仍保留机身长按返回。
    reset(neutral);games=0;add(neutral,-1);add((pad_sample_t){0},-1);
    menu_loop();assert(!launched && drawn && resets==1);
    reset(neutral);games=0;add(neutral,10);
    menu_loop();assert(!launched && drawn);
    return 0;
}
'''
work = Path(tempfile.mkdtemp(prefix='menu-navigation-', dir=ROOT / 'build'))
unit = work / 'menu.c'
unit.write_text(prefix + functions + suffix, encoding='utf-8')
exe = work / ('menu.exe' if os.name == 'nt' else 'menu')
subprocess.run([os.getenv('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Imain', str(unit), 'main/gb_input.c', '-o', str(exe)], cwd=ROOT, check=True)
subprocess.run([str(exe)], check=True)
print('MENU_NAVIGATION_PASS')
