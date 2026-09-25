/*
 * objectdetection.c - LNC Object Detection module
 * See objectdetection.h for the public API and the class-level overview.
 */
#include "objectdetection.h"
#include "main.h"
#include "event.h"
#include "cmsis_os.h"
#include <stdbool.h>

/* Woken for two different reasons -- an edge arrived while the object
 * was considered absent (EDGE), or TIM5 timed out with 10 s of no
 * edges (TIMEOUT). Only the task itself ever writes `present`, so a
 * plain bool read from either ISR is safe -- no writer race. */
#define OBJDET_FLAG_EDGE    (1U << 0)
#define OBJDET_FLAG_TIMEOUT (1U << 1)

/* Noise-rejection burst filter -- last resort after hardware isolation
 * (separate power tap, de-stacking) wasn't feasible with what's on
 * hand. A real IR remote transmission is a dense burst (a full NEC
 * frame produces 60+ edges within ~67ms); ambient/electrical noise is
 * assumed sparser. Instead of treating the very first edge as a
 * detection, require at least BURST_THRESHOLD edges within
 * BURST_WINDOW_MS of each other before accepting it as real.
 *
 * Empirically tuned from hardware logs (2026-09-06): routine SD-write
 * noise typically produces ~8-14 accepted edges per write, but has been
 * observed spiking to 21 on at least one occasion (a threshold of 20
 * was too tight a margin above that and let it through as a false
 * detection -- confirmed by burst_edge_count printing exactly 21 at the
 * moment of that false trigger). A real remote click produces 200+. 50
 * sits with a much wider margin above the observed noise ceiling and
 * still well below the signal floor. Re-tune both if noise still gets
 * through, or if a genuine press stops being recognized -- this is a
 * heuristic, not a real protocol decode, so it's not guaranteed against
 * noise that happens to also be bursty. */
#define BURST_WINDOW_MS   60u
#define BURST_THRESHOLD   80u

/* Speed-based rejection, ahead of the burst filter above -- SPI runs
 * at MHz rates (edges every fraction of a microsecond); a VS1838B's
 * *demodulated* output only changes at the modulation envelope rate,
 * whose shortest real pulse element (NEC) is ~562us. An edge arriving
 * faster than MIN_EDGE_SPACING_US after the previous one physically
 * cannot be a real demodulated IR transition -- reject it outright,
 * before it even counts toward a burst. Timestamped via TIM2 (already
 * a free-running 1us counter for DHT11) since HAL_GetTick()'s 1ms
 * resolution is far too coarse to tell these apart. */
#define MIN_EDGE_SPACING_US 300u

struct ObjectDetection {
    osThreadId_t task_handle;
    bool present;
    uint32_t last_edge_us;          /* TIM2 timestamp of the last accepted edge */
    uint32_t burst_first_edge_tick; /* HAL_GetTick() of the current candidate burst's first edge */
    uint32_t burst_edge_count;      /* edges seen so far in that candidate burst */
};

static struct ObjectDetection g_objdet;

/* ===============================================================
 * ISR-safe entry points -- both do the minimum: a plain register
 * write or a thread-flag signal, nothing that can block or touch
 * the SD card.
 * =============================================================== */

void objdet_on_edge(void) //called form HAL_GPIO_EXTI_Callback() in events.c
{
    uint32_t now_us;
    uint32_t now_tick;

    /* Speed check first, unconditionally -- an edge too fast to be a
     * real demodulated IR transition is treated as pure noise and has
     * zero effect on anything below. */
    now_us = __HAL_TIM_GET_COUNTER(&htim2);

    if (now_us < g_objdet.last_edge_us) {
        /* TIM2 is shared with DHT11's bit-banging, which zeroes it
         * repeatedly mid-read (~85 times per 5 s Monitor round) -- or,
         * far more rarely, it genuinely wrapped after ~71 minutes of
         * free-running. Either way the gap since the last edge can't
         * be trusted: plain unsigned subtraction would underflow into
         * a huge number that wrongly looks "properly spaced" instead
         * of impossibly fast. Reject this one edge, but still record
         * it as the new baseline so the next edge is measured
         * correctly again. */
        g_objdet.last_edge_us = now_us;
        return;
    }
    if (now_us - g_objdet.last_edge_us < MIN_EDGE_SPACING_US) {
        return;
    }
    g_objdet.last_edge_us = now_us;

    /* Burst check runs regardless of `present` -- refreshing an
     * existing detection needs the same proof as starting a new one.
     * Routine SD-write noise passes the speed check (its edges are
     * properly spaced) but isn't dense enough to form a real burst;
     * without this check here, that alone was enough to keep
     * resetting TIM5 forever and permanently lock `present` true,
     * since it happens every 5s -- well under the 10s timeout. */
    now_tick = HAL_GetTick();
    if (now_tick - g_objdet.burst_first_edge_tick > BURST_WINDOW_MS) {
        /* too long since the last edge -- this starts a new candidate burst */
        g_objdet.burst_first_edge_tick = now_tick;
        g_objdet.burst_edge_count = 1;
    } else {
        g_objdet.burst_edge_count++;
    }

    if (g_objdet.burst_edge_count < BURST_THRESHOLD) {
        return; /* not a confirmed burst yet -- don't touch TIM5 or present */
    }

    __HAL_TIM_SET_COUNTER(&htim5, 0);

    if (!g_objdet.present) {
        (void)osThreadFlagsSet(g_objdet.task_handle, OBJDET_FLAG_EDGE); /* ISR context: == FreeRTOS xTaskNotifyFromISR() */
    }
}

void objdet_on_timeout(void) //called form HAL_TIM_PeriodElapsedCallback() in buzzer.c
{
    (void)osThreadFlagsSet(g_objdet.task_handle, OBJDET_FLAG_TIMEOUT); /* ISR context: == FreeRTOS xTaskNotifyFromISR() */
}

/* ===============================================================
 * Task -- the only place that actually calls Event, since that's
 * where touching the SD card through FatFS is safe.
 * =============================================================== */

static void objdet_task(void *argument)
{
    (void)argument;

    for (;;) {
        uint32_t flags = osThreadFlagsWait(OBJDET_FLAG_EDGE | OBJDET_FLAG_TIMEOUT,
                                            osFlagsWaitAny, osWaitForever); /* == FreeRTOS xTaskNotifyWait() */

        if ((flags & OBJDET_FLAG_EDGE) && !g_objdet.present) {
            g_objdet.present = true;
            event_object_detected();
        }
        if ((flags & OBJDET_FLAG_TIMEOUT) && g_objdet.present) {
            g_objdet.present = false;
            event_object_cleared();
        }
    }
}

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

ObjectDetection *objdet_create(void)
{
    /* Same reasoning as monitor.c's stack bump: event_object_detected()/
     * cleared() do a full SD-card write (write_events_file(), event.c)
     * and call comm_send() (now a 98-byte comm_tx_item_t, grown from 34
     * when COMM_MAX_VALUE went 32->96 for the query feature), both
     * nested on this task. 256*4 (1024 bytes) predates that growth. */
    const osThreadAttr_t task_attr = {
        .name = "objDetTask",
        .stack_size = 256 * 16,
        .priority = osPriorityNormal,
    };

    g_objdet.present = false;
    g_objdet.last_edge_us = 0;
    g_objdet.burst_first_edge_tick = 0;
    g_objdet.burst_edge_count = 0;

    g_objdet.task_handle = osThreadNew(objdet_task, NULL, &task_attr);
    if (g_objdet.task_handle == NULL) {
        return NULL;
    }

    if (HAL_TIM_Base_Start_IT(&htim5) != HAL_OK) {
        return NULL;
    }

    return &g_objdet;
}

void objdet_destroy(ObjectDetection *o)
{
    if (o == NULL) {
        return;
    }
    HAL_TIM_Base_Stop_IT(&htim5);
}
