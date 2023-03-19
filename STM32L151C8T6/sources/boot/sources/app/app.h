#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/*----------------------------------------------------------------------------*
 *  DECLARE: Common definitions
 *  Note: 
 *----------------------------------------------------------------------------*/
#ifndef APP_START_ADDR
#define APP_START_ADDR                  ( 0x08003000 )
#define BOOT_START_ADDR                 ( 0x08000000 )
#endif

#define NORMAL_START_ADDRESS			(APP_START_ADDR)
#define BOOT_START_ADDRESS			    (BOOT_START_ADDR)

#define MCU_PLATFROM                     "Stm32l151c8t6"
#define FIRMWARE_VERSION 		         "v1.0-Plus"
#define HARDWARE_VERSION 		         "v2.0"

#define BOOT_VER                         ("1.0")

#define NUM_OF_BYTE_FIRMWARE_UPLOAD      ( 256 )

/* Function prototypes -------------------------------------------------------*/
extern int boot_app();

#ifdef __cplusplus
}
#endif

#endif /* __APP_H__ */
