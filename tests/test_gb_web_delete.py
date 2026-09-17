"""Exercise the production deletion handler with isolated HTTP/storage faults."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/gb_web.c').read_text(encoding='utf-8')
handler = source[source.index('static esp_err_t delete_handler('):
                 source.index('\ngb_web_status_t gb_web_status(')]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { int unused; } httpd_req_t;
typedef struct { uint8_t sha256[32]; } gb_rom_entry_t;
static gb_rom_entry_t entries[2];
static bool origin, ready, catalog_ok, cleanup_ok;
static unsigned count, deleted, cleaned;
static const char *id;
static char status[64], response[256];
static bool same_origin(httpd_req_t *r) { (void)r; return origin; }
static int httpd_resp_set_status(httpd_req_t *r, const char *s) { (void)r; strcpy(status,s); return 0; }
static int httpd_resp_sendstr(httpd_req_t *r, const char *s) { (void)r; snprintf(response,sizeof(response),"%s",s); return 0; }
static int httpd_req_get_url_query_str(httpd_req_t *r,char *q,size_t n) { (void)r; (void)q; (void)n; return 0; }
static int httpd_query_key_value(const char *q,const char *key,char *v,size_t n) { (void)q; (void)key; snprintf(v,n,"%s",id); return 0; }
static size_t gb_storage_count(void) { return count; }
static const gb_rom_entry_t *gb_storage_entry(size_t i) { return i < count ? &entries[i] : NULL; }
static bool gb_saves_ready(void) { return ready; }
static bool gb_storage_delete(size_t i) { assert(i < count); deleted++; return catalog_ok; }
static bool gb_saves_delete(const uint8_t hash[32]) { assert(deleted && catalog_ok); assert(!memcmp(hash,entries[0].sha256,32)); cleaned++; return cleanup_ok; }
'''
suffix = r'''
static void reset(void) {
    origin=ready=catalog_ok=cleanup_ok=true; count=1; deleted=cleaned=0;
    id="0"; strcpy(status,"200 OK"); response[0]=0;
    memset(entries,0,sizeof(entries));
}
int main(void) {
    httpd_req_t req={0};
    reset(); delete_handler(&req); assert(deleted==1 && cleaned==1 && !strcmp(status,"200 OK"));
    reset(); count=2; delete_handler(&req); assert(deleted==1 && cleaned==0);
    reset(); ready=false; delete_handler(&req); assert(!deleted && !cleaned && !strncmp(status,"503",3));
    reset(); catalog_ok=false; delete_handler(&req); assert(deleted==1 && !cleaned && !strncmp(status,"500",3));
    reset(); cleanup_ok=false; delete_handler(&req); assert(deleted==1 && cleaned==1 && !strncmp(status,"500",3));
    reset(); origin=false; delete_handler(&req); assert(!deleted && !cleaned && !strncmp(status,"403",3));
    const char *invalid[]={"", "-1", "1", "0x", "99999999999999"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        reset(); id=invalid[i]; delete_handler(&req);
        assert(!deleted && !cleaned && !strncmp(status,"400",3));
    }
    puts("Deletion handler: PASS (isolated HTTP/storage faults; not device HTTP)");
}
'''
work = Path(tempfile.mkdtemp(prefix='web-delete-', dir=ROOT / 'build'))
unit = work / 'handler.c'
unit.write_text(prefix + handler + suffix, encoding='utf-8')
exe = work / ('handler.exe' if os.name == 'nt' else 'handler')
subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                str(unit), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
