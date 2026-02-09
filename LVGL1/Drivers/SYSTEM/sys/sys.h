/**
 ****************************************************************************************************
 * @file        sys.h
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-07-19
 * @brief       ϵͳ��ʼ������(����ʱ������/�жϹ���/GPIO���õ�)
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� ������ F767������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 20220719
 * ��һ�η���
 ****************************************************************************************************
 */

#ifndef _SYS_H
#define _SYS_H

#include "stm32f7xx.h"
#include "core_cm7.h"
#include "stm32f7xx_hal.h"

/* 兼容老工程的类型别名 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef volatile uint8_t  vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
typedef volatile uint64_t vu64;


/**
 * SYS_SUPPORT_OS���ڶ���ϵͳ�ļ����Ƿ�֧��OS
 * 0,��֧��OS
 * 1,֧��OS
 */
#define SYS_SUPPORT_OS         0

#define ON   1
#define OFF  0
#define      Write_Through()    do{ *(__IO uint32_t*)0XE000EF9C = 1UL << 2; }while(0)     /* Cache͸дģʽ */

void sys_cache_enable(void);                                                              /* ʹ��STM32F7��L1-Cahce */
uint8_t sys_stm32_clock_init(uint32_t plln, uint32_t pllm, uint32_t pllp, uint32_t pllq); /* ����ϵͳʱ�� */
uint8_t get_icache_sta(void);                                                              /* �ж�I_Cache�Ƿ�� */
uint8_t get_dcache_sta(void);                                                              /* �ж�I_Dache�Ƿ�� */


/* ����Ϊ��ຯ�� */
void sys_wfi_set(void);             /* ִ��WFIָ�� */
void sys_intx_disable(void);        /* �ر������ж� */
void sys_intx_enable(void);         /* ���������ж� */
void sys_msr_msp(uint32_t addr);    /* ����ջ����ַ */

#endif

