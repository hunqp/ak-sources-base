#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "io_cfg.h"
#include "sys_cfg.h"

void ledLifeInit() {
	GPIO_InitTypeDef GPIO_InitStructure = { 0 };

	RCC_AHBPeriphClockCmd(IO_LED_LIFE_CLOCK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = IO_LED_LIFE_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(IO_LED_LIFE_PORT, &GPIO_InitStructure);
}

void ledLifeOn() {
	GPIO_ResetBits(IO_LED_LIFE_PORT, IO_LED_LIFE_PIN);
}

void ledLifeOff() {
	GPIO_SetBits(IO_LED_LIFE_PORT, IO_LED_LIFE_PIN);
}

void blinkLedLife() {
	static uint8_t Invert_Status = 0;

	Invert_Status = !Invert_Status;
	
	GPIO_WriteBit(IO_LED_LIFE_PORT, IO_LED_LIFE_PIN, !Invert_Status);
}

void flashSpiInit() {
	GPIO_InitTypeDef GPIO_InitStructure = { 0 };
	SPI_InitTypeDef  SPI_InitStructure = { 0 };
	
	RCC_APB2PeriphClockCmd(FLASH_SPI_CLOCK, ENABLE);
	RCC_AHBPeriphClockCmd(IO_FLASH_SPI_CLOCK, ENABLE);

	GPIO_PinAFConfig(IO_FLASH_SPI_PORT, GPIO_PinSource5, GPIO_AF_SPI1);
	GPIO_PinAFConfig(IO_FLASH_SPI_PORT, GPIO_PinSource6, GPIO_AF_SPI1);
	GPIO_PinAFConfig(IO_FLASH_SPI_PORT, GPIO_PinSource7, GPIO_AF_SPI1);

	GPIO_InitStructure.GPIO_Pin   = IO_FLASH_SPI_CLK_PIN | IO_FLASH_SPI_MISO_PIN | IO_FLASH_SPI_MOSI_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_40MHz;
	GPIO_Init(IO_FLASH_SPI_PORT, &GPIO_InitStructure);

	SPI_InitStructure.SPI_Direction 		= SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_DataSize 			= SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL 				= SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA 				= SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS 				= SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	SPI_InitStructure.SPI_FirstBit 			= SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial 	= 7;
	SPI_InitStructure.SPI_Mode				= SPI_Mode_Master;
  	SPI_Init(FLASH_SPI, &SPI_InitStructure);

	SPI_Cmd(FLASH_SPI, ENABLE);
}

void flashCsInit() {
	GPIO_InitTypeDef GPIO_InitStructure = { 0 };

	RCC_AHBPeriphClockCmd(IO_FLASH_CS_CLOCK, ENABLE);

	GPIO_InitStructure.GPIO_Pin = IO_FLASH_CS_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(IO_FLASH_CS_PORT, &GPIO_InitStructure);
}

void setFlashCsLow() {
	GPIO_ResetBits(IO_FLASH_CS_PORT, IO_FLASH_CS_PIN);
}

void setFlashCsHigh() {
	GPIO_SetBits(IO_FLASH_CS_PORT, IO_FLASH_CS_PIN);
}

uint8_t transfeFlashSpiData(uint8_t data) {
	uint16_t rxtxData = (uint16_t)data;

	/* Waiting send idle then send data */
	while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_TXE) == RESET);
	SPI_I2S_SendData(FLASH_SPI, rxtxData);

	/* Waiting conplete received data */
	while (SPI_I2S_GetFlagStatus(FLASH_SPI, SPI_I2S_FLAG_RXNE) == RESET);
	rxtxData = (uint8_t)SPI_I2S_ReceiveData(FLASH_SPI);

	return (uint8_t)rxtxData;
}

