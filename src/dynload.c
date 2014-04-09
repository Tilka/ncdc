/* ncdc - NCurses Direct Connect client

  Copyright (c) 2014 Tillmann Karras

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "ncdc.h"
#include "dynload.h"

#ifdef USE_GEOIP

#if INTERFACE
// function pointer types
typedef GEOIP_API GeoIP *(*GeoIP_open_type_t)(int type, int flags);
typedef GEOIP_API const char *(*GeoIP_country_code_by_addr_t)(GeoIP *gi, const char *addr);
typedef GEOIP_API const char *(*GeoIP_country_code_by_addr_v6_t)(GeoIP *gi, const char *addr);
#endif

// function pointers
GeoIP_open_type_t GeoIP_open_type_f;
GeoIP_country_code_by_addr_t GeoIP_country_code_by_addr_f;
GeoIP_country_code_by_addr_v6_t GeoIP_country_code_by_addr_v6_f;

// library handle
GModule *libgeoip;

void __attribute__((constructor)) dynamic_loading_init() {
  libgeoip = g_module_open("libGeoIP.so.1", G_MODULE_BIND_LOCAL | G_MODULE_BIND_LAZY);
  if(libgeoip) {
    if(!(g_module_symbol(libgeoip, "GeoIP_open_type", (gpointer *)&GeoIP_open_type_f) &&
         g_module_symbol(libgeoip, "GeoIP_country_code_by_addr", (gpointer *)&GeoIP_country_code_by_addr_f) &&
         g_module_symbol(libgeoip, "GeoIP_country_code_by_addr_v6", (gpointer *)&GeoIP_country_code_by_addr_v6_f)
    )) {
      g_module_close(libgeoip);
      libgeoip = NULL;
    }
  }
}

void __attribute__((destructor)) dynamic_loading_deinit() {
  if(libgeoip)
    g_module_close(libgeoip);
}

#endif // USE_GEOIP
