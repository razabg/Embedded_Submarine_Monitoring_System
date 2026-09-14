#include "sdfatfs.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

/* FATFS and FIL each carry a 512-byte sector buffer (_MAX_SS in ffconf.h),
   so keeping them as plain locals overflows a normal RTOS task stack the
   moment these functions are entered. Static storage keeps them off the
   stack entirely.

   That static storage is shared, mutable state -- originally safe because
   every caller was on the same task (Monitor/Event/Log all wrote from
   their own task context, serialized by nothing but happenstance). That
   stopped being true once TLV_TAG_QUERY_DATA/QUERY_EVENTS started calling
   into this module from Communication's own RX task (log_on_frame()/
   event_on_frame()) while Monitor's/Event's tasks keep writing on their
   own independent schedules -- two tasks can now genuinely call into here
   at the same time. s_sd_lock below serializes every public function in
   this file so only one caller ever touches s_fs/s_fil at once, regardless
   of which task it's calling from. */
static FATFS s_fs;
static FIL s_fil;
static osMutexId_t s_sd_lock;

void SDFatFS_Init(void)
{
    s_sd_lock = osMutexNew(NULL);
}

static FRESULT sd_mount(void)
{
    return f_mount(&s_fs, "", 1);
}

static void sd_unmount(void)
{
    f_mount(NULL, "", 0);
}

FRESULT SDFatFS_SaveData(const char *filename, const void *data, UINT len)
{
    UINT written;
    FRESULT fres;

    osMutexAcquire(s_sd_lock, osWaitForever);

    fres = sd_mount();
    if (fres != FR_OK) {
        // printf("SD mount failed (%i)\r\n", fres);
        osMutexRelease(s_sd_lock);
        return fres;
    }

    /* FA_OPEN_APPEND creates the file if it's missing and seeks to its
       end if it already exists, so earlier saved data is kept intact. */
    fres = f_open(&s_fil, filename, FA_WRITE | FA_OPEN_APPEND);
    if (fres != FR_OK) {
        // printf("SD open failed (%i)\r\n", fres);
        sd_unmount();
        osMutexRelease(s_sd_lock);
        return fres;
    }

    fres = f_write(&s_fil, data, len, &written);
    f_close(&s_fil);
    sd_unmount();
    osMutexRelease(s_sd_lock);

    if (fres != FR_OK) {
        // printf("SD write failed (%i)\r\n", fres);
        return fres;
    }

    // printf("Wrote %u bytes to %s\r\n", written, filename);
    return FR_OK;
}

FRESULT SDFatFS_SaveString(const char *filename, const char *str)
{
    return SDFatFS_SaveData(filename, str, (UINT)strlen(str));
}

FRESULT SDFatFS_PrintFile(const char *filename)
{
    char line[128];
    FRESULT fres;

    osMutexAcquire(s_sd_lock, osWaitForever);

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        osMutexRelease(s_sd_lock);
        return fres;
    }

    fres = f_open(&s_fil, filename, FA_READ);
    if (fres != FR_OK) {
        printf("SD open failed (%i)\r\n", fres);
        sd_unmount();
        osMutexRelease(s_sd_lock);
        return fres;
    }

    printf("---- %s ----\r\n", filename);
    while (f_gets(line, sizeof(line), &s_fil)) {
        printf("%s", line);
    }

    f_close(&s_fil);
    sd_unmount();
    osMutexRelease(s_sd_lock);
    return FR_OK;
}

FRESULT SDFatFS_ForEachLine(const char *filename, SDFatFS_LineCallback cb, void *ctx)
{
    char line[128];
    FRESULT fres;

    osMutexAcquire(s_sd_lock, osWaitForever);

    fres = sd_mount();
    if (fres != FR_OK) {
        osMutexRelease(s_sd_lock);
        return fres;
    }

    fres = f_open(&s_fil, filename, FA_READ);
    if (fres != FR_OK) {
        sd_unmount();
        osMutexRelease(s_sd_lock);
        return fres;
    }

    while (f_gets(line, sizeof(line), &s_fil)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        cb(line, ctx);

        /* Give lower-priority tasks (Monitor, Keep-Alive, Object
         * Detection -- all osPriorityNormal, below comm_rx_task's
         * osPriorityAboveNormal) a real chance to run between lines on
         * a long query, instead of comm_rx_task holding the CPU for
         * the whole scan. osDelay (not osThreadYield) is required here:
         * osThreadYield only hands off to same-priority tasks, it can
         * never let a lower-priority one run. s_sd_lock stays held
         * across this delay -- the file handle stays open for the
         * whole scan, so any other task needing the SD card (e.g.
         * Monitor's log_write()) still waits its turn, it just isn't
         * starved of CPU time to do its non-SD work (sensor sampling)
         * in the meantime. See CLAUDE.md / watchdog.c. */
        osDelay(1);
    }

    f_close(&s_fil);
    sd_unmount();
    osMutexRelease(s_sd_lock);
    return FR_OK;
}

FRESULT SDFatFS_DeleteFile(const char *filename)
{
    FRESULT fres;

    osMutexAcquire(s_sd_lock, osWaitForever);

    fres = sd_mount();
    if (fres != FR_OK) {
        // printf("SD mount failed (%i)\r\n", fres);
        osMutexRelease(s_sd_lock);
        return fres;
    }

    fres = f_unlink(filename);
    sd_unmount();
    osMutexRelease(s_sd_lock);

    if (fres != FR_OK) {
        // printf("SD delete failed (%i)\r\n", fres);
        return fres;
    }

    // printf("Deleted %s\r\n", filename);
    return FR_OK;
}

FRESULT SDFatFS_ListFiles(void)
{
    DIR dir;
    FILINFO info;
    FRESULT fres;

    osMutexAcquire(s_sd_lock, osWaitForever);

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        osMutexRelease(s_sd_lock);
        return fres;
    }

    fres = f_opendir(&dir, "/");
    if (fres != FR_OK) {
        printf("SD opendir failed (%i)\r\n", fres);
        sd_unmount();
        osMutexRelease(s_sd_lock);
        return fres;
    }

    printf("---- SD card contents ----\r\n");
    for (;;) {
        fres = f_readdir(&dir, &info);
        if (fres != FR_OK || info.fname[0] == 0) {
            break;
        }
        printf("%-13s %10lu bytes\r\n", info.fname, (unsigned long)info.fsize);
    }

    f_closedir(&dir);
    sd_unmount();
    osMutexRelease(s_sd_lock);
    return fres;
}
