/**
 ****************************************************************************************************
 * @file        usart.c
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

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"


/* ���ʹ��os,����������ͷ�ļ�����. */
#if SYS_SUPPORT_OS
#include "os.h"         /* os ʹ�� */
#endif

/******************************************************************************************/
/* �������´���, ֧��printf����, ������Ҫѡ��use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)            /* ʹ��AC6������ʱ */
__asm(".global __use_no_semihosting\n\t");  /* ������ʹ�ð�����ģʽ */
__asm(".global __ARM_use_no_argv \n\t");    /* AC6����Ҫ����main����Ϊ�޲�����ʽ�����򲿷����̿��ܳ��ְ�����ģʽ */

#else
/* ʹ��AC5������ʱ, Ҫ�����ﶨ��__FILE �� ��ʹ�ð�����ģʽ */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* ��ʹ�ð�����ģʽ��������Ҫ�ض���_ttywrch\_sys_exit\_sys_command_string����,��ͬʱ����AC6��AC5ģʽ */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* ����_sys_exit()�Ա���ʹ�ð�����ģʽ */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE �� stdio.h���涨��. */
FILE __stdout;

/* �ض���fputc����, printf�������ջ�ͨ������fputc����ַ��������� */
int fputc(int ch, FILE *f)
{
    while ((USART_UX->ISR & 0X40) == 0);    /* �ȴ���һ���ַ�������� */

    USART_UX->TDR = (uint8_t)ch;            /* ��Ҫ���͵��ַ� ch д�뵽DR�Ĵ��� */
    return ch;
}
#endif
/******************************************************************************************/

#if USART_EN_RX     /* ���ʹ���˽��� */

/* ���ջ���, ���USART_REC_LEN���ֽ�. */
uint8_t g_usart_rx_buf[USART_REC_LEN];

/*  ����״̬
 *  bit15��      ������ɱ�־
 *  bit14��      ���յ�0x0d
 *  bit13~0��    ���յ�����Ч�ֽ���Ŀ
*/
uint16_t g_usart_rx_sta = 0;

uint8_t g_rx_buffer[RXBUFFERSIZE];    /* HAL��ʹ�õĴ��ڽ��ջ��� */
uint8_t g_rx_buffer3[RXBUFFERSIZE];   /* USART3 接收缓冲区 */

UART_HandleTypeDef g_uart1_handle;    /* UART��� */
UART_HandleTypeDef g_uart3_handle;    /* UART3 句柄 */

static volatile uart_rx_source_t g_uart_last_rx_port = UART_SRC_UNKNOWN;

uart_rx_source_t usart_get_last_rx_port(void)
{
    return g_uart_last_rx_port;
}

/* UART 中断进入计数（用于底层接收调试） */
volatile uint32_t g_uart_isr_cnt = 0;
volatile uint32_t g_uart_err_ore = 0;
volatile uint32_t g_uart_err_fe = 0;
volatile uint32_t g_uart_err_ne = 0;
volatile uint32_t g_uart_err_pe = 0;
volatile uint32_t g_uart_pause_until_ms = 0;

/*
 * 弱定义：默认不处理收到的字节。
 * 用户工程(例如 User/main.c)可实现同名函数，接管每个收到的字节。
 */
__weak void usart_rx_byte_hook(uint8_t byte)
{
    (void)byte;
}


/**
 * @brief       ����X��ʼ������
 * @param       baudrate: ������, �����Լ���Ҫ���ò�����ֵ
 * @note        ע��: ����������ȷ��ʱ��Դ, ���򴮿ڲ����ʾͻ������쳣.
 *              �����USART��ʱ��Դ��sys_stm32_clock_init()�������Ѿ����ù���.
 * @retval      ��
 */
void usart_init(uint32_t baudrate)
{
    g_uart1_handle.Instance = USART_UX;                    /* USART1 */
    g_uart1_handle.Init.BaudRate = baudrate;               /* ������ */
    if (UART_DEFAULT_PARITY == UART_PARITY_NONE) {
        g_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;  /* 8位数据 */
    } else {
        g_uart1_handle.Init.WordLength = UART_WORDLENGTH_9B;  /* 8位数据 + 1位校验 */
    }
    g_uart1_handle.Init.StopBits = UART_DEFAULT_STOPBITS;  /* 一个停止位 */
    g_uart1_handle.Init.Parity = UART_DEFAULT_PARITY;      /* 奇/偶校验 */
    g_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;   /* ��Ӳ������ */
    g_uart1_handle.Init.Mode = UART_MODE_TX_RX;            /* �շ�ģʽ */
    HAL_UART_Init(&g_uart1_handle);                        /* 初始化 USART1 */
    
    /* �ú����Ὺ�������жϣ���־λUART_IT_RXNE���������ý��ջ����Լ����ջ��������������� */
    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
}

void usart3_init(uint32_t baudrate)
{
    g_uart3_handle.Instance = USART3;
    g_uart3_handle.Init.BaudRate = baudrate;
    if (UART_DEFAULT_PARITY == UART_PARITY_NONE) {
        g_uart3_handle.Init.WordLength = UART_WORDLENGTH_8B;
    } else {
        g_uart3_handle.Init.WordLength = UART_WORDLENGTH_9B;
    }
    g_uart3_handle.Init.StopBits = UART_DEFAULT_STOPBITS;
    g_uart3_handle.Init.Parity = UART_DEFAULT_PARITY;
    g_uart3_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart3_handle.Init.Mode = UART_MODE_TX_RX;
    HAL_UART_Init(&g_uart3_handle);

    HAL_UART_Receive_IT(&g_uart3_handle, (uint8_t *)g_rx_buffer3, RXBUFFERSIZE);
}

/**
 * @brief       UART�ײ��ʼ������
 * @param       huart: UART�������ָ��
 * @note        �˺����ᱻHAL_UART_Init()����
 *              ���ʱ��ʹ�ܣ��������ã��ж�����
 * @retval      ��
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init_struct;
    if(huart->Instance == USART_UX)                                 /* USART2 MSP 初始化 */
    {
        USART_UX_CLK_ENABLE();                                      /* USART2 时钟使能 */
        USART_TX_GPIO_CLK_ENABLE();                                 /* ��������ʱ��ʹ�� */
        USART_RX_GPIO_CLK_ENABLE();                                 /* ��������ʱ��ʹ�� */

        gpio_init_struct.Pin = USART_TX_GPIO_PIN;                   /* TX 引脚 */
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                    /* �������� */
        gpio_init_struct.Pull = GPIO_PULLUP;                        /* ���� */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;              /* ���� */
        gpio_init_struct.Alternate = USART_TX_GPIO_AF;              /* 复用为 USART2 */
        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);       /* ��ʼ���������� */

        gpio_init_struct.Pin = USART_RX_GPIO_PIN;                   /* RX 引脚 */
        gpio_init_struct.Alternate = USART_RX_GPIO_AF;              /* 复用为 USART2 */
        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);       /* ��ʼ���������� */

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART_UX_IRQn);                          /* 使能 USART2 中断 */
        HAL_NVIC_SetPriority(USART_UX_IRQn, 3, 3);                  /* ��ռ���ȼ�3�������ȼ�3 */
#endif
    }
    else if(huart->Instance == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        gpio_init_struct.Pin = GPIO_PIN_10;                          /* PB10: TX */
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;
        gpio_init_struct.Pull = GPIO_PULLUP;
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio_init_struct.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOB, &gpio_init_struct);

        gpio_init_struct.Pin = GPIO_PIN_11;                          /* PB11: RX */
        gpio_init_struct.Alternate = GPIO_AF7_USART3;
        HAL_GPIO_Init(GPIOB, &gpio_init_struct);

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART3_IRQn);
        HAL_NVIC_SetPriority(USART3_IRQn, 3, 3);
#endif
    }
}

/**
 * @brief       Rx����ص�����
 * @param       huart: UART�������ָ��
 * @retval      ��
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART_UX)                           /* ����Ǵ���1 */
    {
        g_uart_last_rx_port = UART_SRC_USART2;
        /* 【关键修复】立即重启下一次接收，防止丢字节！ */
        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);

        /* 清除可能的错误标志，避免后续接收停止 */
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_OREFLAG(huart);
            g_uart_err_ore++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_FE)) {
            __HAL_UART_CLEAR_FEFLAG(huart);
            g_uart_err_fe++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_NE)) {
            __HAL_UART_CLEAR_NEFLAG(huart);
            g_uart_err_ne++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_PE)) {
            __HAL_UART_CLEAR_PEFLAG(huart);
            g_uart_err_pe++;
        }
        
        /* 无论是否使用示例的\r\n字符串接收，都把字节上送给应用层(用于协议解析) */
        usart_rx_byte_hook(g_rx_buffer[0]);

        /* 旧的 \r\n 行接收逻辑（默认关闭，避免与新协议解析重复运行） */
#if USART_LEGACY_LINE_RX
        if((g_usart_rx_sta & 0x8000) == 0)                    /* ����δ��� */
        {
            if(g_usart_rx_sta & 0x4000)                       /* ���յ���0x0d */
            {
                if(g_rx_buffer[0] != 0x0a) 
                {
                    g_usart_rx_sta = 0;                       /* ���մ���,���¿�ʼ */
                }
                else 
                {
                    g_usart_rx_sta |= 0x8000;                 /* ��������� */
                }
            }
            else                                              /* ��û�յ�0X0D */
            {
                if(g_rx_buffer[0] == 0x0d)
                {
                    g_usart_rx_sta |= 0x4000;
                }
                else
                {
                    g_usart_rx_buf[g_usart_rx_sta & 0X3FFF] = g_rx_buffer[0] ;
                    g_usart_rx_sta++;
                    if(g_usart_rx_sta > (USART_REC_LEN - 1))
                    {
                        g_usart_rx_sta = 0;                   /* �������ݴ���,���¿�ʼ���� */
                    }
                }
            }
        }
#endif
        /* 【已移到函数开头】HAL_UART_Receive_IT 必须第一时间执行 */
    }
    else if(huart->Instance == USART3)
    {
        g_uart_last_rx_port = UART_SRC_USART3;
        HAL_UART_Receive_IT(&g_uart3_handle, (uint8_t *)g_rx_buffer3, RXBUFFERSIZE);

        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_OREFLAG(huart);
            g_uart_err_ore++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_FE)) {
            __HAL_UART_CLEAR_FEFLAG(huart);
            g_uart_err_fe++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_NE)) {
            __HAL_UART_CLEAR_NEFLAG(huart);
            g_uart_err_ne++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_PE)) {
            __HAL_UART_CLEAR_PEFLAG(huart);
            g_uart_err_pe++;
        }

        usart_rx_byte_hook(g_rx_buffer3[0]);
    }
}

/**
 * @brief       UART错误回调，防止接收因错误中断而停止
 * @param       huart: UART句柄
 * @retval      无
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART_UX)
    {
        uint32_t now = HAL_GetTick();
        static uint32_t last_tick = 0;
        static uint16_t burst = 0;

        if ((now - last_tick) <= 100) {
            if (burst < 0xFFFF) {
                burst++;
            }
        } else {
            burst = 1;
        }
        last_tick = now;

        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_OREFLAG(huart);
            g_uart_err_ore++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_FE)) {
            __HAL_UART_CLEAR_FEFLAG(huart);
            g_uart_err_fe++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_NE)) {
            __HAL_UART_CLEAR_NEFLAG(huart);
            g_uart_err_ne++;
        }
        if (__HAL_UART_GET_FLAG(huart, UART_FLAG_PE)) {
            __HAL_UART_CLEAR_PEFLAG(huart);
            g_uart_err_pe++;
        }
        /* 连续错误过快：暂停一小段时间，避免错误中断风暴拖死主循环 */
        if (burst >= 20) {
            g_uart_pause_until_ms = now + 200;
            __HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);
            __HAL_UART_DISABLE_IT(huart, UART_IT_ERR);
            HAL_UART_AbortReceive(huart);
            return;
        }

        /* 发生错误时重新启动接收，避免“只收一包”后停止 */
        HAL_UART_AbortReceive(huart);
        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
    }
}

void usart_rx_recover_if_needed(void)
{
    if (g_uart_pause_until_ms == 0) {
        return;
    }

    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - g_uart_pause_until_ms) >= 0) {
        g_uart_pause_until_ms = 0;
        __HAL_UART_ENABLE_IT(&g_uart1_handle, UART_IT_RXNE);
        __HAL_UART_ENABLE_IT(&g_uart1_handle, UART_IT_ERR);
        HAL_UART_AbortReceive(&g_uart1_handle);
        HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
    }
}

/**
 * @brief       ����1�жϷ�����
 * @param       ��
 * @retval      ��
 */
void USART_UX_IRQHandler(void)
{ 
#if SYS_SUPPORT_OS                        /* ʹ��OS */
    OSIntEnter();    
#endif

    g_uart_isr_cnt++; /* 统计中断进入次数 */
    
    HAL_UART_IRQHandler(&g_uart1_handle); /* ����HAL���жϴ������ú��� */


#if SYS_SUPPORT_OS                  /* ʹ��OS */
    OSIntExit();
#endif

}

#endif
 

 




