/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#define _BSD_SOURCE
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#else
#include <endian.h>
#endif
#include "int.h"

int nc_mp_parse_int (char **pbuf, int *plen, int *result) {
    char *buf;
    int len;
    unsigned char marker;

    buf = *pbuf;
    len =  *plen;
    if (len < 1)
        return 0;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if ((marker & 0x80) == 0) {
        /*  Positive fixnum  */
        *result = marker;
    } else if ((marker & 0xe0) == 0xe0) {
        /*  Negative fixnum  */
        *result = (signed char)marker;
    } else {
        switch (marker) {
        case 0xCC:  /*  8-bit unsigned integer  */
            if (len < 1)
                return 0;
            *result = (unsigned char)*buf;
            buf += 1;
            len -= 1;
            break;
        case 0xCD:  /*  16-bit unsigned integer  */
            if (len < 2)
                return 0;
            *result = (int)be16toh(*(uint16_t*)buf);
            buf += 2;
            len -= 2;
            break;
        case 0xCE:  /*  32-bit unsigned integer  */
            if (len < 4)
                return 0;
            *result = (int)be32toh(*(uint32_t*)buf);
            buf += 4;
            len -= 4;
            break;
        case 0xD0:  /*  8-bit signed integer  */
            if (len < 1)
                return 0;
            *result = (char)*buf;
            buf += 1;
            len -= 1;
            break;
        case 0xD1:  /*  16-bit signed integer  */
            if (len < 2)
                return 0;
            *result = (int)(int16_t)be16toh(*(uint16_t*)buf);
            buf += 2;
            len -= 2;
            break;
        case 0xD2:  /*  32-bit signed integer  */
            if (len < 4)
                return 0;
            *result = (int)(int32_t)be32toh(*(uint32_t*)buf);
            buf += 4;
            len -= 4;
            break;
        /*  We treat 64-bit values as error  */
        default:
            return 0;
        }
    }
    *pbuf = buf;
    *plen = len;
    return 1;
}

int nc_mp_parse_string (char **pbuf, int *plen, char **result, int *reslen) {
    char *buf;
    int len;
    int strlen;
    unsigned char marker;

    buf = *pbuf;
    len =  *plen;
    if (len < 1)
        return 0;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if((marker & 0xe0) == 0xa0) {
        strlen = marker & ~0xe0;
    } else {
        switch (marker) {
        case 0xD9:  /*  8-bit unsigned integer  */
            if (len < 1)
                return 0;
            strlen = (unsigned char)*buf;
            buf += 1;
            len -= 1;
            break;
        case 0xDA:  /*  16-bit unsigned integer  */
            if (len < 2)
                return 0;
            strlen = (int)be16toh(*(uint16_t*)buf);
            buf += 2;
            len -= 2;
            break;
        case 0xDB:  /*  32-bit unsigned integer  */
            if (len < 4)
                return 0;
            strlen = (int)be32toh(*(uint32_t*)buf);
            buf += 4;
            len -= 4;
            break;
        default:
            return 0;
        }
    }

    if (len < strlen)
        return 0;

    *result = buf;
    *reslen = strlen;
    *pbuf = buf + strlen;
    *plen = len - strlen;
    return 1;
}

int nc_mp_parse_array (char **pbuf, int *plen, int *arrlen) {
    char *buf;
    int len;
    unsigned char marker;

    buf = *pbuf;
    len =  *plen;
    if (len < 1)
        return 0;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if((marker & 0xf0) == 0x90) {
        *arrlen = marker & ~0xf0;
    } else {
        switch (marker) {
        case 0xDC:  /*  16-bit unsigned integer  */
            if (len < 2)
                return 0;
            *arrlen = (int)be16toh(*(uint16_t*)buf);
            buf += 2;
            len -= 2;
            break;
        case 0xDD:  /*  32-bit unsigned integer  */
            if (len < 4)
                return 0;
            *arrlen = (int)be32toh(*(uint32_t*)buf);
            buf += 4;
            len -= 4;
            break;
        default:
            return 0;
        }
    }


    *pbuf = buf;
    *plen = len;
    return 1;
}

int nc_mp_parse_mapping (char **pbuf, int *plen, int *maplen) {
    char *buf;
    int len;
    unsigned char marker;

    buf = *pbuf;
    len =  *plen;
    if (len < 1)
        return 0;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if((marker & 0xf0) == 0x80) {
        *maplen = marker & ~0xf0;
    } else {
        switch (marker) {
        case 0xDE:  /*  16-bit unsigned integer  */
            if (len < 2)
                return 0;
            *maplen = (int)be16toh(*(uint16_t*)buf);
            buf += 2;
            len -= 2;
            break;
        case 0xDF:  /*  32-bit unsigned integer  */
            if (len < 4)
                return 0;
            *maplen = (int)be32toh(*(uint32_t*)buf);
            buf += 4;
            len -= 4;
            break;
        default:
            return 0;
        }
    }


    *pbuf = buf;
    *plen = len;
    return 1;
}

int nc_mp_skip_value (char **pbuf, int *plen) {
    char *buf;
    int len;
    unsigned char marker;
    uint32_t maplen;
    uint32_t arrlen;
    uint32_t strlen;
    uint32_t i;

    buf = *pbuf;
    len =  *plen;
    if (len < 1)
        return 0;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if(marker < 0x80 || marker >= 0xE0) {  /*  fixint  */
        /*  Marker byte is a value, do nothing  */
    } else if (marker >= 0x80 && marker <= 0x8F) {  /*  fixmapping  */
        maplen = marker & 0x0F;
        for (i = 0; i < maplen; ++i) {
            if (!nc_mp_skip_value (&buf, &len))  /*  key  */
                return 0;
            if (!nc_mp_skip_value (&buf, &len))  /*  value  */
                return 0;
        }
    } else if (marker >= 0x90 && marker <= 0x9F) {  /*  fixarray  */
        arrlen = marker & 0x0F;
        for (i = 0; i < arrlen; ++i) {
            if (!nc_mp_skip_value (&buf, &len))
                return 0;
        }
    } else if (marker >= 0xA0 && marker <= 0xBF) {  /*  fixstr  */
        strlen = marker & 0x1F;
        if (len < (int)strlen)
            return 0;
        len -= strlen;
        buf += strlen;
    } else {
        switch (marker) {
        case 0xC0: break; /*  NIL  */
        case 0xC2: break; /*  False  */
        case 0xC3: break; /*  True  */
        case 0xC7:  /*  ext 8  */
            len -= 1;  /*  skip type byte  */
            buf += 1;
        case 0xC4:  /*  bin 8  */
        case 0xD9:  /*  str 8  */
            if(len < 1)
                return 0;
            strlen = (unsigned char)*buf;
            len -= 1 + strlen;
            buf += 1 + strlen;
            break;
        case 0xC8:  /*  ext 16  */
            len -= 1;  /*  skip type byte  */
            buf += 1;
        case 0xC5:  /*  bin 16  */
        case 0xDA:  /*  str 16  */
            if(len < 2)
                return 0;
            strlen = be16toh(*(uint16_t*)buf);
            len -= 2 + strlen;
            buf += 2 + strlen;
            break;
        case 0xC9:  /*  ext 32  */
            len -= 1;  /*  skip type byte  */
            buf += 1;
        case 0xC6:  /*  bin 32  */
        case 0xDB:  /*  str 32  */
            if(len < 4)
                return 0;
            strlen = be32toh(*(uint32_t*)buf);
            len -= 4 + strlen;
            buf += 4 + strlen;
            break;
        case 0xCA:  /*  float 32  */
            len -= 4;
            buf += 4;
            break;
        case 0xCB:  /*  float 64  */
            len -= 8;
            buf += 8;
            break;
        case 0xCC:  /*  uint 8  */
        case 0xD0:  /*  int 8  */
            len -= 1;
            buf += 1;
            break;
        case 0xCD:  /*  uint 16  */
        case 0xD1:  /*  int 16  */
            len -= 2;
            buf += 2;
            break;
        case 0xCE:  /*  uint 32  */
        case 0xD2:  /*  int 32  */
            len -= 4;
            buf += 4;
            break;
        case 0xCF:  /*  uint 64  */
        case 0xD3:  /*  int 64  */
            len -= 8;
            buf += 8;
            break;
        case 0xD4:  /*  fixext 1  */
            len -= 2;
            buf += 2;
            break;
        case 0xD5:  /*  fixext 2  */
            len -= 3;
            buf += 3;
            break;
        case 0xD6:  /*  fixext 4  */
            len -= 5;
            buf += 5;
            break;
        case 0xD7:  /*  fixext 8  */
            len -= 9;
            buf += 9;
            break;
        case 0xD8:  /*  fixext 16  */
            len -= 17;
            buf += 17;
            break;
        case 0xDC:  /*  array 16  */
            if (len < 2)
                return 0;
            arrlen = be16toh(*(uint16_t*)buf);
            len -= 2;
            buf += 2;
            for (i = 0; i < arrlen; ++i) {
                if (!nc_mp_skip_value (&buf, &len))
                    return 0;
            }
            break;
        case 0xDD:  /*  array 32  */
            if (len < 4)
                return 0;
            arrlen = be32toh(*(uint16_t*)buf);
            len -= 4;
            buf += 4;
            for (i = 0; i < arrlen; ++i) {
                if (!nc_mp_skip_value (&buf, &len))
                    return 0;
            }
            break;
        case 0xDE:  /*  map 16  */
            if (len < 2)
                return 0;
            maplen = be16toh(*(uint16_t*)buf);
            len -= 2;
            buf += 2;
            for (i = 0; i < maplen; ++i) {
                if (!nc_mp_skip_value (&buf, &len))  /*  key  */
                    return 0;
                if (!nc_mp_skip_value (&buf, &len))  /*  value  */
                    return 0;
            }
            break;
        case 0xDF:  /*  map 32  */
            if (len < 4)
                return 0;
            maplen = be32toh(*(uint16_t*)buf);
            len -= 4;
            buf += 4;
            for (i = 0; i < maplen; ++i) {
                if (!nc_mp_skip_value (&buf, &len))  /*  key  */
                    return 0;
                if (!nc_mp_skip_value (&buf, &len))  /*  value  */
                    return 0;
            }
            break;
        default:
            return 0;
        }
    }
    if (len < 0)
        return 0;
    *pbuf = buf;
    *plen = len;
    return 1;
}
