"""Exercise production catalog handlers and storage with isolated HTTP/Flash I/O."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/gb_web.c').read_text(encoding='utf-8')
listing = source[source.index('static esp_err_t list_handler('):
                 source.index('\nstatic int receive_chunk(')]
handler = source[source.index('static esp_err_t rename_handler('):
                 source.index('\ngb_web_status_t gb_web_status(')]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#include "gb_storage.c"
#define ESP_FAIL -1
typedef struct { int unused; } httpd_req_t;
static unsigned char flash[256 * 1024];
static const esp_partition_t partition = {sizeof(flash)};
static bool origin, ready, cleanup_ok, fail_erase, fail_readback;
static unsigned erased, cleaned;
static const char *request_token, *request_name;
static char status[64], response[4096];
const esp_partition_t *esp_partition_find_first(int type, esp_partition_subtype_t sub, const char *name) { (void)type; (void)sub; (void)name; return &partition; }
esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t n) { assert(off+n<=p->size); if(fail_readback && off>=data_end()) return -1; memcpy(dst,flash+off,n); return 0; }
esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t n) { assert(off+n<=p->size); memcpy(flash+off,src,n); return 0; }
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t n) { assert(off+n<=p->size); erased++; if(fail_erase) return -1; memset(flash+off,255,n); return 0; }
esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t off, size_t n, int mode, const void **out, esp_partition_mmap_handle_t *h) { (void)p; (void)n; (void)mode; *out=flash+off; *h=1; return 0; }
void esp_partition_munmap(esp_partition_mmap_handle_t h) { (void)h; }
static bool same_origin(httpd_req_t *r) { (void)r; return origin; }
static int httpd_resp_set_status(httpd_req_t *r, const char *s) { (void)r; strcpy(status,s); return 0; }
static int httpd_resp_sendstr(httpd_req_t *r, const char *s) { (void)r; snprintf(response,sizeof(response),"%s",s); return 0; }
static int httpd_resp_set_type(httpd_req_t *r, const char *s) { (void)r; (void)s; return 0; }
static int httpd_resp_sendstr_chunk(httpd_req_t *r, const char *s) { (void)r; if(s) { assert(strlen(response)+strlen(s)<sizeof(response)); strcat(response,s); } return 0; }
static int httpd_req_get_url_query_str(httpd_req_t *r,char *q,size_t n) { (void)r; (void)q; (void)n; return 0; }
static int httpd_query_key_value(const char *q,const char *key,char *v,size_t n) { (void)q; const char *value=!strcmp(key,"token")?request_token:request_name; if(!value || strlen(value)>=n) return -1; strcpy(v,value); return 0; }
static bool gb_saves_ready(void) { return ready; }
static bool gb_saves_delete(const uint8_t hash[32]) { assert(gb_storage_catalog_trusted()); for(size_t i=0;i<gb_storage_count();i++) assert(memcmp(hash,gb_storage_entry(i)->sha256,32)); cleaned++; return cleanup_ok; }
'''
suffix = r'''
static char token[GB_STORAGE_TOKEN_BYTES];
static int receive(void *context, void *dst, size_t n) { (void)context; memset(dst,0,n); return (int)n; }
static void add(const char *name) { assert(gb_storage_upload(name,32768,receive,NULL)); }
static void fresh(size_t index) { assert(gb_storage_token(index,token)); request_token=token; }
static void response_reset(void) { strcpy(status,"200 OK"); response[0]=0; }
static void reset(void) {
    origin=ready=cleanup_ok=true; fail_erase=fail_readback=false; cleaned=0;
    memset(flash,255,sizeof(flash)); assert(gb_storage_init()); add("original");
    erased=0; fresh(0); request_name="renamed"; response_reset();
}
int main(void) {
    httpd_req_t req={0};
    reset(); list_handler(&req); assert(strstr(response,token) && strstr(response,"original"));
    reset(); delete_handler(&req); assert(gb_storage_count()==0 && cleaned==1 && !strcmp(status,"200 OK"));
    reset(); add("copy"); fresh(0); delete_handler(&req); assert(gb_storage_count()==1 && cleaned==0);
    reset(); ready=false; delete_handler(&req); assert(!erased && !cleaned && !strncmp(status,"503",3));
    reset(); fail_erase=true; delete_handler(&req); assert(gb_storage_count()==1 && !cleaned && !strncmp(status,"500",3));
    reset(); cleanup_ok=false; delete_handler(&req); assert(gb_storage_count()==0 && cleaned==1 && !strncmp(status,"500",3));
    reset(); origin=false; delete_handler(&req); assert(!erased && !cleaned && !strncmp(status,"403",3));
    const char *invalid[]={"", "-1", "1", "0x", "99999999999999"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        reset(); request_token=invalid[i]; delete_handler(&req);
        assert(!erased && !cleaned && !strncmp(status,"409",3));
    }
    reset(); request_token=NULL; delete_handler(&req);
    assert(!erased && !cleaned && !strncmp(status,"400",3));

    // Both clients saw the same two entries. Removing row zero must invalidate
    // both old tokens, including the token for the row now shifted to zero.
    reset(); add("copy");
    char old_first[GB_STORAGE_TOKEN_BYTES], old_second[GB_STORAGE_TOKEN_BYTES];
    assert(gb_storage_token(0,old_first) && gb_storage_token(1,old_second));
    request_token=old_first; delete_handler(&req);
    unsigned before=erased;
    assert(gb_storage_count()==1 && cleaned==0);
    response_reset(); request_token=old_first; delete_handler(&req);
    assert(erased==before && !cleaned && !strncmp(status,"409",3));
    response_reset(); request_token=old_second; rename_handler(&req);
    assert(erased==before && !strncmp(status,"409",3));
    assert(!strcmp(gb_storage_entry(0)->name,"copy"));
    response_reset(); fresh(0); rename_handler(&req);
    assert(!strcmp(status,"200 OK") && !strcmp(gb_storage_entry(0)->name,"renamed"));

    // Re-uploading an identical ROM/name into the same slot is a new catalog
    // generation. A pre-delete browser action must not delete the new upload.
    reset(); strcpy(old_first,token); delete_handler(&req); add("original");
    before=erased; unsigned cleanup_before=cleaned;
    response_reset(); request_token=old_first; delete_handler(&req);
    assert(erased==before && cleaned==cleanup_before && !strncmp(status,"409",3));
    assert(gb_storage_count()==1);

    reset(); fail_readback=true; delete_handler(&req);
    assert(gb_storage_needs_reload() && !cleaned && !strncmp(status,"503",3));
    before=erased; response_reset(); rename_handler(&req);
    assert(erased==before && !strncmp(status,"503",3));
    response_reset(); delete_handler(&req); assert(erased==before && !cleaned);
    response_reset(); list_handler(&req); assert(!strncmp(status,"503",3));
    fail_readback=false; assert(gb_storage_init()); assert(gb_storage_count()==0);
    puts("Catalog handlers: PASS (real handlers/storage, mocked HTTP/Flash; not device HTTP)");
}
'''
work = Path(tempfile.mkdtemp(prefix='web-delete-', dir=ROOT / 'build'))
unit = work / 'handler.c'
unit.write_text(prefix + listing + handler + suffix, encoding='utf-8')
exe = work / ('handler.exe' if os.name == 'nt' else 'handler')
subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT / 'main'),
                '-I' + str(ROOT / 'components/gb_core/include'),
                str(unit), str(ROOT / 'main/gb_name.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
