/**
 ****************************************************************************************************
 * @file        usart.h
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.1
 * @date        2023-06-05
 * @brief       ���ڳ�ʼ������(һ���Ǵ���1)��֧��printf
 * @license     Copyright (c) 2022-2032, �������������ӿƼ����޹�˾
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
 * V1.1 20230605
 * �޸�SYS_SUPPORT_OS���ִ���, ����ͷ�ļ��ĳ�:"os.h"
 * ɾ��USART_UX_IRQHandler()�����ĳ�ʱ�������޸�HAL_UART_RxCpltCallback()
 *
 ****************************************************************************************************
 */

#ifndef __USART_H
#define __USART_H

#include "stdio.h"
#include "./SYSTEM/sys/sys.h"

/* ========================= 串口选择 =========================
 * 目标：板载 COM2 RS232 母口
 * 对应：USART2 (PA2=TX, PA3=RX)
 */

#define USART_TX_GPIO_PORT              GPIOA
#define USART_TX_GPIO_PIN               GPIO_PIN_2
#define USART_TX_GPIO_AF                GPIO_AF7_USART2
#define USART_TX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define USART_RX_GPIO_PORT              GPIOA
#define USART_RX_GPIO_PIN               GPIO_PIN_3
#define USART_RX_GPIO_AF                GPIO_AF7_USART2
#define USART_RX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define USART_UX                        USART2
#define USART_UX_IRQn                   USART2_IRQn
#define USART_UX_IRQHandler             USART2_IRQHandler
#define USART_UX_CLK_ENABLE()           do{ __HAL_RCC_USART2_CLK_ENABLE(); }while(0)

/*******************************************************************************************************/

#define USART_REC_LEN   200                     /* �����������ֽ��� 200 */
#define USART_EN_RX     1                       /* ʹ�ܣ�1��/��ֹ��0������1���� */
#define RXBUFFERSIZE    1                       /* �����С */

/* ===================== 串口协议默认参数 =====================
 * 与 SQMWD_Tablet 保持一致：38400 波特率 + 奇校验
 * 如需与 XCOM 手动测试，请同步修改 XCOM 参数或修改此处宏。
 */
#define UART_DEFAULT_BAUDRATE  38400
#define UART_DEFAULT_PARITY    UART_PARITY_ODD
#define UART_DEFAULT_STOPBITS  UART_STOPBITS_1
#define UART_DEFAULT_WORDLEN   UART_WORDLENGTH_8B

/* 旧的 \r\n 行接收逻辑（USMART 依赖此缓冲区）
 * - 为避免与新协议解析重复运行，默认关闭
 * - 如需恢复旧逻辑，将其改为 1
 */
#define USART_LEGACY_LINE_RX  0

extern UART_HandleTypeDef g_uart1_handle;       /* UART��� */

/* UART 中断进入计数（用于底层接收调试） */
extern volatile uint32_t g_uart_isr_cnt;
/* UART 错误计数（用于底层接收调试） */
extern volatile uint32_t g_uart_err_ore;
extern volatile uint32_t g_uart_err_fe;
extern volatile uint32_t g_uart_err_ne;
extern volatile uint32_t g_uart_err_pe;
extern volatile uint32_t g_uart_pause_until_ms;

extern uint8_t  g_usart_rx_buf[USART_REC_LEN];  /* ���ջ���,���USART_REC_LEN���ֽ�.ĩ�ֽ�Ϊ���з� */
extern uint16_t g_usart_rx_sta;                 /* ����״̬��� */
extern uint8_t g_rx_buffer[RXBUFFERSIZE];       /* HAL��USART����Buffer */

/*
 * 串口接收字节Hook：
 * - 在 HAL_UART_RxCpltCallback() 中，每收到 1 个字节就会调用一次
 * - 默认实现为空(弱定义)，用户工程可在别处实现同名函数以接管收到的字节流
 * - 用途：把字节流写入应用层 ring buffer(obuf)，供协议解析器处理
 */
void usart_rx_byte_hook(uint8_t byte);


void usart_init(uint32_t baudrate);             /* ���ڳ�ʼ������ */
void usart_rx_recover_if_needed(void);

#endif







