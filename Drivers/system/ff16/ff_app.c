#include "main.h"
#include "ff_app.h"
#include "usart_driver.h"
#include "ucmd.h"

uint8_t *xFileType_List[5] = {NULL, "dir", "mp3", "txt", NULL};

typedef enum 
{
    dir,
    mp3,
    txt
}FileType_t;

/* List contents of a directory */

void list_curdir (void)
{
    FRESULT res;
    DIR dir;
    FILINFO fno;
    int nfile, ndir;

    char *path = "./";

    res = f_opendir(&dir, path);                   /* Open the directory */
    if (res == FR_OK) {
        nfile = ndir = 0;
        for (;;) {
            res = f_readdir(&dir, &fno);           /* Read a directory item */
            if (fno.fname[0] == 0) break;          /* Error or end of dir */
            if (fno.fattrib & AM_DIR) {            /* It is a directory */
                iUsart1_Printf("   <DIR>   %s\n", fno.fname);
                ndir++;
            } else {                               /* It is a file */
                /* fsize 是 FSIZE_t(64位, FF_FS_EXFAT=1)，%u 只消费低 32 位，
                   导致 %s 读到 fsize 高 32 位(NULL/垃圾指针) → hardfault */
                iUsart1_Printf("%10lu %s\n", (unsigned long)fno.fsize, fno.fname);
                nfile++;
            }
        }
        f_closedir(&dir);
        iUsart1_Printf("%d dirs, %d files.\n", ndir, nfile);
    } else {
        iUsart1_Printf("Failed to open \"%s\". (%u)\n", path, res);
    }
}
UCMD_W0( list_curdir )

uint16_t vFindFileType( char * Path )
{
    uint32_t i = 0, j = 0, ulIndex = 0;
    uint16_t usTypeIndex = 0;

    for (i = 0; Path[i] != '\r' && Path[i] != '\n' && Path[i] != '\0'; i++)
    {
        if (Path[i] == '.')
        {
            ulIndex = ++i;

            for (; Path[i] != '\r' && Path[i] != '\n' && Path[i] != '\0'; i++);
            Path[i] = '\0';

            break;
        }
    }

    if (Path[i - 1] == '/')
    {
        return 1;
    }

    for (i = 2; xFileType_List[i] != NULL; i++)
    {
        for (j = 0; j < 3; j++)
        {
            if (Path[ulIndex + j] != xFileType_List[i][j])
            {
                break;
            }
            goto findtype;
        }
    }

    return usTypeIndex;

findtype:
    usTypeIndex = i;
    return usTypeIndex;
}

void open_file( char *Path )
{
    uint16_t usTypeIndex = 0;

    usTypeIndex = vFindFileType( Path );
    if (!usTypeIndex)
    {
        iUsart1_Printf( "Failed to open \"%s\".\n", Path );
        iUsart1_Printf( "This file is neither a directory nor a file supported by this system.\n" );
        return;
    }

    FRESULT res = FR_OK;

    switch (usTypeIndex)
    {
        case 1: res = f_chdir( (const TCHAR *)Path );

                if (res == FR_OK)
                {
                    uint8_t dir_name[512] = {0};
                    f_getcwd( (TCHAR *)dir_name, 512 );
                    iUsart1_Printf( "->%s/\n", dir_name );
                }
                else
                {
                    iUsart1_Printf("Failed to open \"%s\". (%u)\n", Path, res);
                }

                break;

        case 2: break;
        case 3: break;
        default: break;
    }
}
UCMD_W1( open_file, s )
