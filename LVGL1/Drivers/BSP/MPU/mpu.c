/**
 ****************************************************************************************************
 * @file        mpu.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-07-19
 * @brief       MPU�ڴ汣�� ��������
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
 *
 ****************************************************************************************************
 */

#include "./BSP/MPU/mpu.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"

extern volatile uint32_t g_boot_stage;


/**
 * @brief       ����ĳ�������MPU����
 * @param       baseaddr: MPU��������Ļ�ַ(�׵�ַ)
 * @param       size:MPU��������Ĵ�С(������32�ı���,��λΪ�ֽ�)
 * @param       rnum:MPU���������,��Χ:0~7,���֧��8����������
 * @param       de:��ָֹ�����;0,����ָ�����;1,��ָֹ�����
 * @param       ap:����Ȩ��,���ʹ�ϵ����:
 *   @arg       0,�޷��ʣ���Ȩ&�û������ɷ��ʣ�
 *   @arg       1,��֧����Ȩ��д����
 *   @arg       2,��ֹ�û�д���ʣ���Ȩ�ɶ�д���ʣ�
 *   @arg       3,ȫ���ʣ���Ȩ&�û����ɷ��ʣ�
 *   @arg       4,�޷�Ԥ��(��ֹ����Ϊ4!!!)
 *   @arg       5,��֧����Ȩ������
 *   @arg       6,ֻ������Ȩ&�û���������д��
 *   @note      ���:STM32F7����ֲ�.pdf,4.6��,Table 89.
 * @param       sen:�Ƿ���������;0,������;1,����
 * @param       cen:�Ƿ�����cache;0,������;1,����
 * @param       ben:�Ƿ���������;0,������;1,����
 * @retval      0, �ɹ�; 1, ����;
 */
uint8_t mpu_set_protection(uint32_t baseaddr, uint32_t size, uint32_t rnum, uint8_t de, uint8_t ap, uint8_t sen, uint8_t cen, uint8_t ben)
{
    MPU_Region_InitTypeDef mpu_region_init_struct;           /* MPU��ʼ����� */
    HAL_MPU_Disable();            /* ����MPU֮ǰ�ȹر�MPU,��������Ժ���ʹ��MPU */

    mpu_region_init_struct.Enable = MPU_REGION_ENABLE;       /* ʹ�ܸñ������� */
    mpu_region_init_struct.Number = rnum;                    /* ���ñ������� */
    mpu_region_init_struct.BaseAddress = baseaddr;           /* ���û�ַ */
    mpu_region_init_struct.Size = size;                      /* ���ñ��������С */
    mpu_region_init_struct.SubRegionDisable = 0X00;          /* ��ֹ������ */
    mpu_region_init_struct.TypeExtField = MPU_TEX_LEVEL0;    /* ����������չ��Ϊlevel0 */
    mpu_region_init_struct.DisableExec = de;                 /* �Ƿ�����ָ�����(������ȡָ��) */
    mpu_region_init_struct.AccessPermission = ap;            /* ���÷���Ȩ�� */
    mpu_region_init_struct.IsShareable = sen;                /* �Ƿ��������� */
    mpu_region_init_struct.IsCacheable = cen;                /* �Ƿ�����cache */
    mpu_region_init_struct.IsBufferable = ben;               /* �Ƿ��������� */
    HAL_MPU_ConfigRegion(&mpu_region_init_struct);           /* ����MPU */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);                  /* ����MPU */
    return 0;
}

/**
 * @brief       ������Ҫ�����Ĵ洢��
 * @note        ����Բ��ִ洢�������MPU����,������ܵ��³��������쳣
 *              ����MCU������ʾ,����ͷ�ɼ����ݳ����ȵ�����
 * @param       ��
 * @retval      ��
 */
void mpu_memory_protection(void)
{
    /* ���������ڲ�SRAM,����SRAM1,SRAM2��DTCM,��512K�ֽ�,����ָ�����,��ֹ����,����cache,��������*/
    mpu_set_protection(0x20000000, MPU_REGION_SIZE_512KB, MPU_REGION_NUMBER1, MPU_INSTRUCTION_ACCESS_ENABLE,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE, MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    /* ����MCU LCD�����ڵ�FMC����,,��64M�ֽ�,����ָ�����,��ֹ����,��ֹcache,��ֹ���� */
    mpu_set_protection(0x60000000, MPU_REGION_SIZE_64MB, MPU_REGION_NUMBER0, MPU_INSTRUCTION_ACCESS_ENABLE,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE, MPU_ACCESS_NOT_CACHEABLE, MPU_ACCESS_NOT_BUFFERABLE);

    /*����SDRAM����,��32M�ֽ�,����ָ�����,��ֹ����,����cache,�������� */
    mpu_set_protection(0XC0000000, MPU_REGION_SIZE_32MB, MPU_REGION_NUMBER2, MPU_INSTRUCTION_ACCESS_ENABLE,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE, MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    /* ��������NAND FLASH����,��256M�ֽ�,����ָ�����,��ֹ����,��ֹcache,��ֹ���� */
    mpu_set_protection(0X80000000, MPU_REGION_SIZE_256MB, MPU_REGION_NUMBER3, MPU_INSTRUCTION_ACCESS_ENABLE,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE, MPU_ACCESS_NOT_CACHEABLE, MPU_ACCESS_NOT_BUFFERABLE);
}

/**
 * @brief       MemManage�������ж�
 *   @note      ������ж��Ժ�,���޷��ָ���������!!
 *
 * @param       ��
 * @retval      ��
 */
void MemManage_Handler(void)
{ 
    LED1(0);                            /* ����LED1(GREEN LED) */
    printf("[MEM] stage=%lu\r\n", (unsigned long)g_boot_stage);
    printf("Mem Access Error!!\r\n");   /* ���������Ϣ */
    delay_ms(1000);
    printf("Soft Reseting...\r\n");     /* ��ʾ�������� */
    delay_ms(1000);
    NVIC_SystemReset();                 /* ����λ */
}
