#define _POSIX_C_SOURCE 200809L
#define GB_SAVES_BASE_PATH "build/sr"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define fsync(fd) (0)
#else
#define make_dir(path) mkdir(path, 0700)
#endif
#include "../main/gb_saves.c"

struct gb_session { uint8_t ram[8192], rtc[10]; bool dirty; };
static unsigned reads, fail_read;
bool gb_storage_catalog_trusted(void) { return true; }
size_t gb_storage_count(void) { return 0; }
const gb_rom_entry_t *gb_storage_entry(size_t i) { (void)i; return NULL; }
size_t gb_port_sram_size(const gb_session_t *s) { return sizeof(s->ram); }
bool gb_port_has_battery(const gb_session_t *s) { (void)s; return true; }
bool gb_port_needs_save(const gb_session_t *s) { return s->dirty; }
void gb_port_mark_saved(gb_session_t *s) { s->dirty=false; }
bool gb_port_read_sram(const gb_session_t *s,size_t off,void *dst,size_t n) {
    if (++reads==fail_read || off>sizeof(s->ram) || n>sizeof(s->ram)-off) return false;
    memcpy(dst,s->ram+off,n); return true;
}
bool gb_port_write_sram(gb_session_t *s,size_t off,const void *src,size_t n) {
    if(off>sizeof(s->ram) || n>sizeof(s->ram)-off) return false;
    memcpy(s->ram+off,src,n); return true;
}
bool gb_port_export_rtc(gb_session_t *s,uint8_t dst[10]) { memcpy(dst,s->rtc,10);return true; }
bool gb_port_import_rtc(gb_session_t *s,const uint8_t src[10]) { memcpy(s->rtc,src,10);return true; }

int main(void) {
    assert(make_dir("build")==0 || errno==EEXIST);
    assert(make_dir(GB_SAVES_BASE_PATH)==0 || errno==EEXIST);
    assert(gb_saves_init());
    uint8_t hash[32]={0x73};
    gb_session_t source={0}, restored={0};
    assert(gb_saves_load(&restored,hash)==GB_SAVE_EMPTY);
    memset(source.ram,0x11,sizeof(source.ram)); source.rtc[0]=7; source.dirty=true;
    assert(gb_saves_write(&source,hash) && !source.dirty);
    unsigned before=reads;
    assert(gb_saves_write(&source,hash) && reads==before);
    memset(source.ram,0x22,sizeof(source.ram)); source.dirty=true;
    assert(gb_saves_write(&source,hash));
    assert(gb_saves_load(&restored,hash)==GB_SAVE_LOADED);
    assert(!memcmp(restored.ram,source.ram,sizeof(source.ram)) && restored.rtc[0]==7);

    // Fail while writing the older slot; the previous good slot must survive.
    memset(source.ram,0x33,sizeof(source.ram)); source.dirty=true;
    fail_read=reads+20;
    assert(!gb_saves_write(&source,hash) && source.dirty);
    fail_read=0;
    assert(gb_saves_load(&restored,hash)==GB_SAVE_LOADED && restored.ram[0]==0x22);
    assert(gb_saves_write(&source,hash));
    assert(gb_saves_load(&restored,hash)==GB_SAVE_LOADED && restored.ram[8191]==0x33);

    char a[32],b[32];save_path(hash,'a',a);save_path(hash,'b',b);
    for(unsigned i=0;i<2;i++) {
        FILE *f=fopen(i?b:a,"wb");assert(f);assert(fwrite("bad",1,3,f)==3);assert(!fclose(f));
    }
    memset(restored.ram,0xaa,sizeof(restored.ram));
    assert(gb_saves_load(&restored,hash)==GB_SAVE_ERROR && restored.ram[0]==0xaa);
    source.dirty=true;
    assert(!gb_saves_write(&source,hash) && source.dirty);
    assert(gb_saves_delete(hash));
    assert(rmdir(GB_SAVES_BASE_PATH)==0);
    puts("Save recovery: PASS (host file faults; not physical power-loss validation)");
}
