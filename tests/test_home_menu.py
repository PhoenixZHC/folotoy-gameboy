"""执行真实主页循环与菜单输入逻辑，覆盖手柄边沿、重连和机身按键。"""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/game_main.c').read_text(encoding='utf-8')
home=source[source.index('static size_t home_loop(void)'):source.index('\n}',source.index('static size_t home_loop(void)'))+2]
helper=source[source.index('static BaseType_t menu_button_receive('):source.index('\n}',source.index('static BaseType_t menu_button_receive('))+2]
home=helper+'\n'+home
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "gb_input.h"
enum {BSP_BTN_UP,BSP_BTN_DOWN,BSP_BTN_OK,BSP_BTN_CLICK,BSP_BTN_LONG};
typedef int BaseType_t;
typedef int TickType_t;
typedef struct {int button,event;} button_message_t;
static int s_buttons;
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
typedef struct {pad_sample_t pad; int board;} event_t;
static event_t events[32];
static pad_sample_t initial;
static int count,frame;
static pad_sample_t gamepad_snapshot(void) {return frame<0?initial:events[frame].pad;}
static int xQueueReceive(int q,button_message_t *m,int timeout) {
    (void)q;assert(timeout<=100 && ++frame<count);
    int board=events[frame].board;
    if(board<0)return 0;
    *m=(button_message_t){board,BSP_BTN_CLICK};return 1;
}
static gb_menu_action_t board_menu_action(button_message_t m) {
    if(m.button==BSP_BTN_UP)return GB_MENU_UP;
    if(m.button==BSP_BTN_DOWN)return GB_MENU_DOWN;
    return GB_MENU_CONFIRM;
}
static int64_t esp_timer_get_time(void) {return (frame+1)*50000;}
static void show_ui(const char *title,const char *hint,const char **items,size_t n,size_t selected,const char *footer) {
    (void)title;(void)hint;(void)items;(void)footer;assert(n==4 && selected<4);
    assert(strcmp(items[1], "游戏管理")==0 && strcmp(items[2], "手柄测试")==0);
    assert(strcmp(items[0],gamepad_snapshot().connected?"游戏列表":"连接手柄")==0);
}
'''
suffix=r'''
static void reset(pad_sample_t pad) {initial=pad;count=0;frame=-1;}
static void add(pad_sample_t pad,int board) {events[count++]=(event_t){pad,board};}
int main(void) {
    pad_sample_t held_select={.connected=true,.view=true}, released_select={.connected=true};
    gb_select_pause_t hold;
    button_message_t msg;
    reset(released_select);
    gb_select_pause_init(&hold,released_select);
    for(int i=0;i<22;i++)add(held_select,-1);
    for(int i=0;i<20;i++)assert(menu_button_receive(&hold,&msg,50)==0);
    assert(menu_button_receive(&hold,&msg,50)==pdTRUE);
    assert(msg.button==BSP_BTN_OK && msg.event==BSP_BTN_LONG);
    assert(menu_button_receive(&hold,&msg,50)==0);
    // Re-enter a parent while Select is still held: do not cascade backwards.
    gb_select_pause_init(&hold,held_select);
    reset(held_select);for(int i=0;i<22;i++)add(held_select,-1);
    for(int i=0;i<22;i++)assert(menu_button_receive(&hold,&msg,50)==0);

    pad_sample_t neutral={.connected=true},down={.connected=true,.down=true},
                 up={.connected=true,.up=true},confirm={.connected=true,.a=true};
    reset(neutral);add(down,-1);add(neutral,-1);add(down,-1);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==2); // 手柄进入测试页。
    reset(down);add(down,-1);add(down,-1);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==0); // 入页持键不移动。
    reset(neutral);add(down,-1);add(down,-1);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==1); // 持续按住不重复跳行。
    reset(neutral);add(up,-1);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==3); // 向上循环到设置。
    reset(neutral);add((pad_sample_t){0},-1);add(confirm,-1);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==0 && frame==3); // 重连按住 A 不误确认。
    reset((pad_sample_t){0});add((pad_sample_t){0},BSP_BTN_DOWN);add((pad_sample_t){0},BSP_BTN_OK);
    assert(home_loop()==1); // 无手柄仍可用机身按键。
    reset(neutral);add(down,BSP_BTN_UP);add(neutral,-1);add(confirm,-1);
    assert(home_loop()==3); // 同帧机身优先，不叠加两个动作。
    return 0;
}
'''
work=Path(tempfile.mkdtemp(prefix='home-menu-',dir=ROOT/'build'))
unit=work/'home.c';unit.write_text(prefix+home+suffix,encoding='utf-8')
exe=work/('home.exe' if os.name=='nt' else 'home')
subprocess.run([os.getenv('CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-Imain',
               str(unit),'main/gb_input.c','-o',str(exe)],cwd=ROOT,check=True)
subprocess.run([str(exe)],check=True)
print('HOME_MENU_PASS')
