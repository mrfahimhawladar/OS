#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <ta sobre.h>

#define TA_UUID \
    { 0x9f4d1e72, 0x6a0b, 0x4a31, \
      { 0x9b, 0x60, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x43 } }

#define TA_FLAGS (TA_FLAG_SINGLE_INSTANCE)
#define TA_STACK_SIZE (8 * 1024)
#define TA_DATA_SIZE (32 * 1024)
#define TA_VERSION "1.0"
#define TA_DESCRIPTION "Fahim OS secure PIN credential verifier"

#endif
