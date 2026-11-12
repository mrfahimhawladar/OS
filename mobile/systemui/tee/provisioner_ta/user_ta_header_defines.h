#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <user_ta_header.h>

#define TA_UUID \
    { 0x28e3b2d4, 0x4a20, 0x4f91, \
      { 0x9a, 0x11, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x50 } }

#define TA_FLAGS (TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION)
#define TA_STACK_SIZE (16 * 1024)
#define TA_DATA_SIZE (32 * 1024)
#define TA_VERSION "1.0"
#define TA_DESCRIPTION "Fahim OS trusted PIN enrollment and confirmation"

#endif
