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
#include <endian.h>
#include "int.h"

int nc_mp_parse_int (char **pbuf, int *plen, int *result) {
    char *buf;
    int len;
    unsigned char marker;

    buf = *pbuf;
    len =  *plen;
    marker = (unsigned char)*buf;
    buf += 1;
    len -= 1;

    if (len < 1)
        return 0;

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
