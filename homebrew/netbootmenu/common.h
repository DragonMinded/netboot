#ifndef __COMMON_H
#define __COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define MESSAGE_SELECTION 0x1000
#define MESSAGE_LOAD_SETTINGS 0x1001
#define MESSAGE_LOAD_SETTINGS_ACK 0x1002
#define MESSAGE_LOAD_SETTINGS_DATA 0x1005
#define MESSAGE_LOAD_PROGRESS 0x1009
#define MESSAGE_UNPACK_PROGRESS 0x100A
#define MESSAGE_SAVE_SETTINGS_DATA 0x1007
#define MESSAGE_SAVE_SETTINGS_ACK 0x1008
#define MESSAGE_SAVE_CONFIG 0x1003
#define MESSAGE_SAVE_CONFIG_ACK 0x1004

#ifdef __cplusplus
}
#endif

#endif
