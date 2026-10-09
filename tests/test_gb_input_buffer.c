#include <assert.h>
#include <stdio.h>
#include "gb_input.h"

// 与修复前 gamepad.c 的 set_sample/snapshot 相同的覆盖语义，用于复现。
#ifdef INPUT_LEGACY_SNAPSHOT
#define gb_input_buffer_reset legacy_reset
#define gb_input_buffer_push legacy_push
#define gb_input_buffer_take legacy_take
static void legacy_reset(gb_input_buffer_t *s, pad_sample_t p, bool enabled) {
    (void)enabled; s->latest = p;
}
static void legacy_push(gb_input_buffer_t *s, pad_sample_t p, int64_t now) {
    (void)now; s->latest = p;
}
static pad_sample_t legacy_take(gb_input_buffer_t *s, int64_t now) {
    (void)now; return s->latest;
}
#endif

static pad_sample_t sample(bool right, bool a, bool b) {
    return (pad_sample_t){.connected=true, .right=right, .a=a, .b=b,
                         .any_button=right || a || b};
}

static void short_taps(void) {
    gb_input_buffer_t s = {0};
    gb_input_buffer_reset(&s, sample(false,false,false), true);
    gb_input_buffer_push(&s, sample(true,false,false), 0);
    assert(gb_input_buffer_take(&s, 0).right);
    // 两次短按都发生在下一个模拟帧之前，同时保持右方向。
    gb_input_buffer_push(&s, sample(true,true,false), 2000);
    gb_input_buffer_push(&s, sample(true,false,false), 4000);
    gb_input_buffer_push(&s, sample(true,true,false), 6000);
    gb_input_buffer_push(&s, sample(true,false,false), 8000);
    for (int i=0; i<4; i++) {
        pad_sample_t p = gb_input_buffer_take(&s, 16000*(i+1));
        assert(p.right && p.a == (i%2 == 0) && !p.b);
    }
    assert(gb_input_buffer_take(&s, 80000).right);
}

static void chords_and_order(void) {
    gb_input_buffer_t s = {0};
    gb_input_buffer_reset(&s, sample(false,false,false), true);
    const uint8_t chord[] = {0x07,0x0d,0x0e};
    pad_sample_t p = gb_input_keyboard(chord, sizeof(chord), 0);
    for (int i=0; i<20; i++) gb_input_buffer_push(&s, p, i);
    p = gb_input_buffer_take(&s, 1000);
    assert(p.right && p.a && p.b && s.count==0);
    gb_input_buffer_push(&s, sample(true,false,true), 2000);
    gb_input_buffer_push(&s, sample(true,false,false), 3000);
    p = gb_input_buffer_take(&s, 16000);
    assert(p.right && !p.a && p.b);
    p = gb_input_buffer_take(&s, 32000);
    assert(p.right && !p.a && !p.b);
    gb_input_buffer_push(&s, sample(true,true,false), 33000);
    gb_input_buffer_push(&s, sample(true,false,true), 34000);
    p = gb_input_buffer_take(&s, 48000);
    assert(p.a && !p.b);
    p = gb_input_buffer_take(&s, 64000);
    assert(!p.a && p.b);
}

static void lifecycle_and_limits(void) {
    gb_input_buffer_t s = {0};
    // 进入游戏/恢复时必须先松开，菜单确认键不得转成攻击。
    gb_input_buffer_reset(&s, sample(false,true,false), true);
    assert(!gb_input_buffer_take(&s, 0).a);
    gb_input_buffer_push(&s, sample(false,false,false), 1000);
    gb_input_buffer_push(&s, sample(false,true,false), 2000);
    assert(gb_input_buffer_take(&s, 3000).a);
    gb_input_buffer_reset(&s, sample(false,true,false), false);
    gb_input_buffer_push(&s, sample(false,false,true), 4000);
    assert(!gb_input_buffer_take(&s, 5000).b);
    gb_input_buffer_reset(&s, sample(false,false,false), true);
    gb_input_buffer_push(&s, sample(true,true,false), 6000);
    gb_input_buffer_push(&s, (pad_sample_t){0}, 7000);
    assert(!gb_input_buffer_take(&s, 8000).connected && s.count==0);
    gb_input_buffer_push(&s, sample(true,true,false), 9000);
    assert(!gb_input_buffer_take(&s, 10000).a);
    gb_input_buffer_push(&s, sample(false,false,false), 11000);
    gb_input_buffer_push(&s, sample(true,true,false), 12000);
    assert(gb_input_buffer_take(&s, 13000).a);
    // 长卡顿后回到最新状态，不补发过期攻击。
    gb_input_buffer_push(&s, sample(true,false,false), 14000);
    gb_input_buffer_push(&s, sample(true,false,true), 15000);
    gb_input_buffer_push(&s, sample(true,false,false), 16000);
    pad_sample_t p = gb_input_buffer_take(&s, 170000);
    assert(p.right && !p.a && !p.b && s.count==0 && s.dropped==3);
    // 超容量时丢弃整段积压，不能丢失松开后永久卡键。
    for (int i=0; i<GB_INPUT_BUFFER_CAPACITY+1; i++)
        gb_input_buffer_push(&s, sample(true,i%2==0,false), 180000+i);
    assert(s.dropped==3+GB_INPUT_BUFFER_CAPACITY);
    gb_input_buffer_push(&s, sample(true,false,false), 181000);
    assert(gb_input_buffer_take(&s, 190000).a);
    p=gb_input_buffer_take(&s, 200000);
    assert(p.right && !p.a && !p.b);
}

static void expiry_boundary(void) {
    gb_input_buffer_t s = {0};
    gb_input_buffer_reset(&s, sample(false,false,false), true);
    gb_input_buffer_push(&s, sample(false,true,false), 1000);
    gb_input_buffer_push(&s, sample(false,false,false), 2000);
    assert(gb_input_buffer_take(&s, 101000).a && s.dropped==0);
    assert(!gb_input_buffer_take(&s, 102000).a && s.dropped==0);
    gb_input_buffer_push(&s, sample(false,true,false), 103000);
    gb_input_buffer_push(&s, sample(false,false,false), 104000);
    assert(!gb_input_buffer_take(&s, 203001).a && s.dropped==2);
}

static void stick_and_actions(void) {
    gb_input_buffer_t s = {0};
    gb_input_buffer_reset(&s, sample(false,false,false), true);
    pad_sample_t p=sample(false,true,false);
    gb_input_left_stick(&p, 511, 0);
    gb_input_buffer_push(&s,p,1000);
    gb_keys_t k=gb_input_map(gb_input_buffer_take(&s,16000));
    assert(k.right && k.a);
    // 新报告回中只释放摇杆方向，仍按住的攻击继续保留。
    p=sample(false,true,false);
    gb_input_left_stick(&p,0,0);
    gb_input_buffer_push(&s,p,17000);
    k=gb_input_map(gb_input_buffer_take(&s,32000));
    assert(!k.right && k.a);
    p=sample(false,false,false);
    gb_input_left_stick(&p,-511,0);
    gb_input_buffer_reset(&s,p,true);
    assert(!gb_input_buffer_take(&s,33000).left);
    p=sample(false,false,false);
    gb_input_left_stick(&p,0,0);
    gb_input_buffer_push(&s,p,34000);
    p=sample(false,true,false);
    gb_input_left_stick(&p,-511,0);
    gb_input_buffer_push(&s,p,35000);
    k=gb_input_map(gb_input_buffer_take(&s,48000));
    assert(k.left && k.a);
}

int main(void) {
    short_taps();
    chords_and_order();
    lifecycle_and_limits();
    expiry_boundary();
    stick_and_actions();
    puts("input buffer: 5 scenarios passed");
    return 0;
}
