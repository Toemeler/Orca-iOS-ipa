/* Orca-iOS-ipa: original no-op hidapi backend for iOS.
 * iOS exposes no general HID access; Orca's 3D-mouse support degrades
 * gracefully when enumeration returns nothing. License: AGPL-3.0. */
#include "hidapi.h"
#include <stddef.h>

int hid_init(void) { return 0; }
int hid_exit(void) { return 0; }
struct hid_device_info *hid_enumerate(unsigned short v, unsigned short p) { (void)v;(void)p; return NULL; }
void hid_free_enumeration(struct hid_device_info *d) { (void)d; }
hid_device *hid_open(unsigned short v, unsigned short p, const wchar_t *s) { (void)v;(void)p;(void)s; return NULL; }
hid_device *hid_open_path(const char *path) { (void)path; return NULL; }
int hid_write(hid_device *d, const unsigned char *b, size_t n) { (void)d;(void)b;(void)n; return -1; }
int hid_read_timeout(hid_device *d, unsigned char *b, size_t n, int ms) { (void)d;(void)b;(void)n;(void)ms; return -1; }
int hid_read(hid_device *d, unsigned char *b, size_t n) { (void)d;(void)b;(void)n; return -1; }
int hid_set_nonblocking(hid_device *d, int nb) { (void)d;(void)nb; return -1; }
int hid_send_feature_report(hid_device *d, const unsigned char *b, size_t n) { (void)d;(void)b;(void)n; return -1; }
int hid_get_feature_report(hid_device *d, unsigned char *b, size_t n) { (void)d;(void)b;(void)n; return -1; }
void hid_close(hid_device *d) { (void)d; }
int hid_get_manufacturer_string(hid_device *d, wchar_t *s, size_t n) { (void)d;(void)s;(void)n; return -1; }
int hid_get_product_string(hid_device *d, wchar_t *s, size_t n) { (void)d;(void)s;(void)n; return -1; }
int hid_get_serial_number_string(hid_device *d, wchar_t *s, size_t n) { (void)d;(void)s;(void)n; return -1; }
int hid_get_indexed_string(hid_device *d, int i, wchar_t *s, size_t n) { (void)d;(void)i;(void)s;(void)n; return -1; }
const wchar_t *hid_error(hid_device *d) { (void)d; return L"hidapi: not supported on iOS"; }
