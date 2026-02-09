#ifndef _NAND_H
#define _NAND_H
#include "sys.h"
/*
 * NAND Flash 驱动头文件（来自 ALIENTEK 示例工程，做兼容保留）
 */
 


#define NAND_MAX_PAGE_SIZE			4096		//����NAND FLASH������PAGE��С��������SPARE������Ĭ��4096�ֽ�
#define NAND_ECC_SECTOR_SIZE		512			//ִ��ECC����ĵ�Ԫ��С��Ĭ��512�ֽ�


//NAND FLASH���������ʱ����
#define NAND_TADL_DELAY				60			//tADL等待延时(加大)
#define NAND_TWHR_DELAY				50			//tWHR等待延时(加大)
#define NAND_TRHW_DELAY				70			//tRHW等待延时(加大)
#define NAND_TPROG_DELAY			800			//tPROG等待延时(加大，接近700us)
#define NAND_TBERS_DELAY			10			//tBERS等待延时(加大)


//NAND���Խṹ��
typedef struct
{
    u16 page_totalsize;     	//ÿҳ�ܴ�С��main����spare���ܺ�
    u16 page_mainsize;      	//ÿҳ��main����С
    u16 page_sparesize;     	//ÿҳ��spare����С
    u8  block_pagenum;      	//ÿ���������ҳ����
    u16 plane_blocknum;     	//ÿ��plane�����Ŀ�����
    u16 block_totalnum;     	//�ܵĿ�����
    u16 good_blocknum;      	//�ÿ�����    
    u16 valid_blocknum;     	//��Ч������(���ļ�ϵͳʹ�õĺÿ�����)
    u32 id;             		//NAND FLASH ID
    u16 *lut;      		   	//LUT���������߼���-������ת��
	u32 ecc_hard;				//Ӳ�����������ECCֵ
	u32 ecc_hdbuf[NAND_MAX_PAGE_SIZE/NAND_ECC_SECTOR_SIZE];//ECCӲ������ֵ������  	
	u32 ecc_rdbuf[NAND_MAX_PAGE_SIZE/NAND_ECC_SECTOR_SIZE];//ECC��ȡ��ֵ������
}nand_attriute;      

extern nand_attriute nand_dev;				//nand��Ҫ�����ṹ�� 

#define NAND_RB  	            HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_6)//NAND Flash����/æ����

#define NAND_ADDRESS			0X80000000	//nand flash�ķ��ʵ�ַ,��NCE3,��ַΪ:0X8000 0000
#define NAND_CMD				1<<16		//��������
#define NAND_ADDR				1<<17		//���͵�ַ

//NAND FLASH����
#define NAND_READID         	0X90    	//��IDָ��
#define NAND_FEATURE			0XEF    	//��������ָ��
#define NAND_RESET          	0XFF    	//��λNAND
#define NAND_READSTA        	0X70   	 	//��״̬
#define NAND_AREA_A         	0X00   
#define NAND_AREA_TRUE1     	0X30  
#define NAND_WRITE0        	 	0X80
#define NAND_WRITE_TURE1    	0X10
#define NAND_ERASE0        	 	0X60
#define NAND_ERASE1         	0XD0
#define NAND_MOVEDATA_CMD0  	0X00
#define NAND_MOVEDATA_CMD1  	0X35
#define NAND_MOVEDATA_CMD2  	0X85
#define NAND_MOVEDATA_CMD3  	0X10

//NAND FLASH״̬
#define NSTA_READY       	   	0X40		//nand�Ѿ�׼����
#define NSTA_ERROR				0X01		//nand����
#define NSTA_TIMEOUT        	0X02		//��ʱ
#define NSTA_ECC1BITERR       	0X03		//ECC 1bit����
#define NSTA_ECC2BITERR       	0X04		//ECC 2bit���ϴ���


//NAND FLASH�ͺźͶ�Ӧ��ID��
#define MT29F4G08ABADA			0XDC909556	//MT29F4G08ABADA
#define MT29F16G08ABABA			0X48002689	//MT29F16G08ABABA

//MPU�������
#define NAND_REGION_NUMBER      MPU_REGION_NUMBER3	    //NAND FLASHʹ��region0
#define NAND_ADDRESS_START      0X80000000              //NAND FLASH�����׵�ַ
#define NAND_REGION_SIZE        MPU_REGION_SIZE_256MB   //NAND FLASH����С

u8 NAND_Init(void);
u8 NAND_ModeSet(u8 mode);
u32 NAND_ReadID(void);
u8 NAND_ReadStatus(void);
u8 NAND_WaitForReady(void);
u8 NAND_Reset(void);
u8 NAND_WaitRB(vu8 rb);
void NAND_Delay(vu32 i);
void NAND_MPU_Config(void);
u8 NAND_ReadPage(u32 PageNum,u16 ColNum,u8 *pBuffer,u16 NumByteToRead);
u8 NAND_ReadPageComp(u32 PageNum,u16 ColNum,u32 CmpVal,u16 NumByteToRead,u16 *NumByteEqual);
u8 NAND_WritePage(u32 PageNum,u16 ColNum,u8 *pBuffer,u16 NumByteToWrite);
u8 NAND_WritePageConst(u32 PageNum,u16 ColNum,u32 cval,u16 NumByteToWrite);
u8 NAND_CopyPageWithoutWrite(u32 Source_PageNum,u32 Dest_PageNum);
u8 NAND_CopyPageWithWrite(u32 Source_PageNum,u32 Dest_PageNum,u16 ColNum,u8 *pBuffer,u16 NumByteToWrite);
u8 NAND_ReadSpare(u32 PageNum,u16 ColNum,u8 *pBuffer,u16 NumByteToRead);
u8 NAND_WriteSpare(u32 PageNum,u16 ColNum,u8 *pBuffer,u16 NumByteToRead);
u8 NAND_EraseBlock(u32 BlockNum);
void NAND_EraseChip(void);

u16 NAND_ECC_Get_OE(u8 oe,u32 eccval);
u8 NAND_ECC_Correction(u8* data_buf,u32 eccrd,u32 ecccl);
#endif
