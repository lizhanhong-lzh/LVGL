/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs (NAND via FTL)                    */
/*-----------------------------------------------------------------------*/
#include "diskio.h"         /* FatFs lower layer API */
#include "ffconf.h"
#include "malloc.h"
#include "nand.h"
#include "ftl.h"

#define NAND_DRV 0

static DSTATUS nand_status = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != NAND_DRV) {
        return STA_NOINIT;
    }
    return nand_status;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    u8 res;

    if (pdrv != NAND_DRV) {
        return STA_NOINIT;
    }

    res = FTL_Init();
    if (res) {
        nand_status = STA_NOINIT;
        return STA_NOINIT;
    }

    nand_status = 0;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    u8 res;

    if (!count) return RES_PARERR;
    if (pdrv != NAND_DRV) return RES_PARERR;
    if (nand_status & STA_NOINIT) return RES_NOTRDY;

    res = FTL_ReadSectors(buff, sector, 512, count);
    return (res == 0) ? RES_OK : RES_ERROR;
}

#if _FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    u8 res;

    if (!count) return RES_PARERR;
    if (pdrv != NAND_DRV) return RES_PARERR;
    if (nand_status & STA_NOINIT) return RES_NOTRDY;

    res = FTL_WriteSectors((u8*)buff, sector, 512, count);
    return (res == 0) ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != NAND_DRV) return RES_PARERR;

    switch(cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(WORD*)buff = nand_dev.page_mainsize / 512;
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(DWORD*)buff = nand_dev.valid_blocknum * nand_dev.block_pagenum * nand_dev.page_mainsize / 512;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return 0;
}

void *ff_memalloc(UINT size)
{
    return (void*)mymalloc(SRAMIN, size);
}

void ff_memfree(void* mf)
{
    myfree(SRAMIN, mf);
}









