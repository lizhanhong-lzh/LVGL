/**
  ******************************************************************************
  * @file    Templates/Src/stm32f7xx.c
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_it.h"
#include "stm32f7xx_hal.h"
#include "./BSP/LED/led.h"
#include "./SYSTEM/usart/usart.h"
#include <stdio.h>

extern volatile uint32_t g_boot_stage;

void hard_fault_handler_c(uint32_t *sp);

static void fault_blink(void)
{
  printf("[FAULT] stage=%lu\r\n", (unsigned long)g_boot_stage);
  for (;;) {
    LED1_TOGGLE();
    for (volatile uint32_t i = 0; i < 200000; i++) {
      __NOP();
    }
  }
}

void hard_fault_handler_c(uint32_t *sp)
{
  uint32_t r0  = sp[0];
  uint32_t r1  = sp[1];
  uint32_t r2  = sp[2];
  uint32_t r3  = sp[3];
  uint32_t r12 = sp[4];
  uint32_t lr  = sp[5];
  uint32_t pc  = sp[6];
  uint32_t psr = sp[7];

  printf("[HARDFAULT] stage=%lu\r\n", (unsigned long)g_boot_stage);
  printf(" r0=%08lX r1=%08lX r2=%08lX r3=%08lX\r\n", r0, r1, r2, r3);
  printf(" r12=%08lX lr=%08lX pc=%08lX psr=%08lX\r\n", r12, lr, pc, psr);
  printf(" CFSR=%08lX HFSR=%08lX DFSR=%08lX\r\n",
         (unsigned long)SCB->CFSR,
         (unsigned long)SCB->HFSR,
         (unsigned long)SCB->DFSR);
  printf(" MMFAR=%08lX BFAR=%08lX\r\n",
         (unsigned long)SCB->MMFAR,
         (unsigned long)SCB->BFAR);

  fault_blink();
}

/** @addtogroup STM32F7xx_HAL_Examples
  * @{
  */

/** @addtogroup Templates
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M7 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
__asm void HardFault_Handler(void)
{
  IMPORT hard_fault_handler_c
  TST LR, #4
  ITE EQ
  MRSEQ R0, MSP
  MRSNE R0, PSP
  B hard_fault_handler_c
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
//void MemManage_Handler(void)
//{
//  /* Go to infinite loop when Memory Manage exception occurs */
//  while (1)
//  {
//  }
//}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  fault_blink();
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  fault_blink();
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/**
  * @brief  This function handles USART3 global interrupt.
  * @param  None
  * @retval None
  */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&g_uart3_handle);
}

/******************************************************************************/
/*                 STM32F7xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f7xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/



/**
  * @}
  */ 

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
