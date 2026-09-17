#include "gb_name.h"

#include <assert.h>
#include <string.h>

int main(void) {
    char name[32];
    assert(gb_name_valid("口袋妖怪蓝"));
    assert(gb_name_url_decode("%E5%8F%A3%E8%A2%8B%E5%A6%96%E6%80%AA%E8%93%9D",
                              name, sizeof(name)));
    assert(strcmp(name, "口袋妖怪蓝") == 0);
    assert(gb_name_url_decode("Pokemon+Blue", name, sizeof(name)));
    assert(strcmp(name, "Pokemon Blue") == 0);
    assert(!gb_name_valid("口袋妖怪蓝精灵宝可梦蓝"));
    assert(!gb_name_url_decode("%C0%AF", name, sizeof(name)));
    assert(!gb_name_url_decode("%E5%8F", name, sizeof(name)));
    assert(!gb_name_url_decode("%00", name, sizeof(name)));
    assert(!gb_name_url_decode("folder%2Fgame", name, sizeof(name)));
    assert(!gb_name_url_decode("%F0%9F%8E%AE", name, sizeof(name)));
    return 0;
}
