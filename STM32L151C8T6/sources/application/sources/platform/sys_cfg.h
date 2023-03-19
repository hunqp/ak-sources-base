#ifndef __SYS_CFG_H
#define __SYS_CFG_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "stm32xx_inc.h"

/*----------------------------------------------------------------------------*
 *  DECLARE: USART Communication boards interface defines
 *  Note: 
 *----------------------------------------------------------------------------*/
#define UART_TERMINAL_CLOCK               	( RCC_APB2Periph_USART1 )
#define UART_TERMINAL                     	( USART1 )
#define UART_TERMINAL_IRQn                	( USART1_IRQn )
#define UART_TERMINAL_BAUD            		( 115200 )

#define IO_UART_TERMINAL_CLOCK				( RCC_AHBPeriph_GPIOA )
#define IO_UART_TERMINAL_PORT             	( GPIOA )
#define UART_TERMINAL_TX_PIN           		( GPIO_Pin_9 )
#define UART_TERMINAL_RX_PIN           		( GPIO_Pin_10 )

#define TERMINAL_BUFFER_SIZE    			(32)

/*----------------------------------------------------------------------------*
 *  DECLARE: TIM6 Period interrupt defines
 *  Note: 
 *----------------------------------------------------------------------------*/
/*
 *								SystemCoreClock
 * TIM_Freq_Counter =	____________________________________
 *						(TIM_Prescaler + 1)x(TIM_Period + 1)
 */
#define TIM6_PRESCALER_VALUE	(40000)
#define TIM6_RELOAD_VALUE		(4000)

/* Typedef -------------------------------------------------------------------*/
typedef struct {
	uint32_t CpuClk;
	uint32_t Tick;
	uint32_t Baudrate;
	uint32_t flashUsed;
	uint32_t sramUsed;
	uint32_t dataUsed;
    uint32_t bssUsed;
	uint32_t stackRemain;
	uint32_t heapSize;
	char Hardware[15];
	char Firmware[15];
} sysStructInfo_t;

/* Extern variables ----------------------------------------------------------*/
extern sysStructInfo_t sysStructInfo;
extern ringBufferChar_t terminalLetterRead;

/* Function prototypes -------------------------------------------------------*/
extern void mcuClockInit(void);
extern void mcuTickInit(void);
extern void terminalInit(void);
extern void updateInfoSystem(void);

extern void watchdogInit(void);
extern void TIM6_Initial(void);
extern void RTC_Initial(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_CFG_H */
