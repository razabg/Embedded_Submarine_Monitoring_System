/*
 * communication.c - LNC Communication module
 *
 * Owns USART2 (huart2) exclusively -- no other firmware module may touch
 * HAL UART calls or huart2 directly. See communication.h for the public
 * API. Everything else in this file is private: comm_send() is the only
 * way in, and Communication's own TX/RX tasks are the only things that
 * ever touch the queues, the semaphore, or the UART peripheral itself.
 */

#include "communication.h"
#include "tlv.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>

/* ===============================================================
 * Module state
 *
 * One static instance (g_comm below) for the program's whole life --
 * there is exactly one USART2, so exactly one Communication. Fields
 * are grouped by which side touches them: TX (comm_send/comm_tx_task)
 * or RX (the ISR/comm_rx_task).
 * =============================================================== */

struct Communication
{
    osMessageQueueId_t txq_high;  /* keep-alive: highest priority, 1-slot mailbox */
    osMessageQueueId_t txq_med;   /* events, acks, time-reply: medium priority */
    osMessageQueueId_t txq_low;   /* data reports, query records: low priority */
    osSemaphoreId_t sem_tx_ready; /* "doorbell": rung once per item queued for TX */

    osMessageQueueId_t rxq_bytes; /* raw incoming bytes, ISR -> RX task */
    uint8_t rx_isr_byte;          /* scratch byte HAL_UART_Receive_IT fills */
    tlv_receiver_t rx_recv;       /* the TLV decoder state machine */

    osThreadId_t tx_task_handle;
    osThreadId_t rx_task_handle;
};

static struct Communication g_comm;

#define COMM_MAX_VALUE 96u
/* Was 32 (generous vs. every fixed-field payload up to that point --
 * biggest was a timestamp + a handful of u16/i16 fields); raised for
 * TLV_TAG_QUERY_RECORD, which forwards a whole stored log/event line
 * as-is rather than a compact struct -- worst case observed is a
 * mode-change event line at ~87 bytes ("[2026-09-06 14:30:00] mode
 * WARNING -> NORMAL (temp=-99C hum=100% light=100% batt=100%)").
 * TLV_MAX_VALUE (255) from tlv.h is still the hard ceiling if a future
 * message needs more. Declared up here, ahead of the public API below,
 * because communication_create() needs sizeof(comm_tx_item_t) to size
 * the TX queues. */

/* One "slot" in each of the 3 TX queues. Just tag + payload, not yet a
 * real TLV frame -- tlv_encode() happens later, inside the TX task. */
typedef struct
{
    uint8_t tag;
    uint8_t len;
    uint8_t value[COMM_MAX_VALUE];
} comm_tx_item_t;

/* ===============================================================
 * Public API: create / destroy
 *
 * Forward declarations only for comm_tx_task/comm_rx_task -- their
 * bodies live further down (TX side / RX side sections below), but
 * osThreadNew() here needs to call them by name, and C requires a
 * function's signature to be known before it's referenced.
 * =============================================================== */

static void comm_tx_task(void *argument);
static void comm_rx_task(void *argument);

Communication *communication_create(void)
{
    const osThreadAttr_t tx_task_attr = {
        .name = "commTx",
        .stack_size = 256 * 4,
        .priority = osPriorityAboveNormal,
    };
    const osThreadAttr_t rx_task_attr = {
        .name = "commRx",
        .stack_size = 256 * 16,
        .priority = osPriorityAboveNormal,
    };

    g_comm.txq_high = osMessageQueueNew(1, sizeof(comm_tx_item_t), NULL);
    g_comm.txq_med = osMessageQueueNew(4, sizeof(comm_tx_item_t), NULL);
    g_comm.txq_low = osMessageQueueNew(4, sizeof(comm_tx_item_t), NULL);
    g_comm.sem_tx_ready = osSemaphoreNew(10, 0, NULL); //high + med + low
    g_comm.rxq_bytes = osMessageQueueNew(128, sizeof(uint8_t), NULL);

    if (g_comm.txq_high == NULL || g_comm.txq_med == NULL || g_comm.txq_low == NULL ||
        g_comm.sem_tx_ready == NULL || g_comm.rxq_bytes == NULL)
    {
        return NULL;
    }

    g_comm.tx_task_handle = osThreadNew(comm_tx_task, NULL, &tx_task_attr);
    g_comm.rx_task_handle = osThreadNew(comm_rx_task, NULL, &rx_task_attr);

    if (g_comm.tx_task_handle == NULL || g_comm.rx_task_handle == NULL)
    {
        return NULL;
    }

    return &g_comm;
}

void communication_destroy(Communication *comm)
{
    if (comm == NULL)
    {
        return;
    }

    if (comm->tx_task_handle != NULL)
    {
        osThreadTerminate(comm->tx_task_handle);
    }
    if (comm->rx_task_handle != NULL)
    {
        osThreadTerminate(comm->rx_task_handle);
    }

    if (comm->txq_high != NULL)
    {
        osMessageQueueDelete(comm->txq_high);
    }
    if (comm->txq_med != NULL)
    {
        osMessageQueueDelete(comm->txq_med);
    }
    if (comm->txq_low != NULL)
    {
        osMessageQueueDelete(comm->txq_low);
    }
    if (comm->rxq_bytes != NULL)
    {
        osMessageQueueDelete(comm->rxq_bytes);
    }
    if (comm->sem_tx_ready != NULL)
    {
        osSemaphoreDelete(comm->sem_tx_ready);
    }
}

/* ===============================================================
 * TX side
 *
 * comm_send() is the public entry point: classifies the tag, queues
 * tag+payload, rings the doorbell. comm_tx_task() is the only thing
 * that ever calls tlv_encode()/HAL_UART_Transmit() -- it sleeps on
 * the doorbell and drains high -> med -> low, one item per wake-up.
 * =============================================================== */

/* Send priority: keep-alive > events/acks/replies > data reports. */
typedef enum
{
    COMM_PRIO_HIGH,
    COMM_PRIO_MED,
    COMM_PRIO_LOW
} comm_prio_t;

/* Classifies a tag into its send priority. Pure lookup, no side effects. */
static comm_prio_t comm_priority_for_tag(uint8_t tag)
{
    switch (tag)
    {
    case TLV_TAG_KEEP_ALIVE:
        return COMM_PRIO_HIGH;

    case TLV_TAG_MODE_CHANGE:
    case TLV_TAG_OBJECT_DETECTED:
    case TLV_TAG_OBJECT_CLEARED:
    case TLV_TAG_STARTUP:
    case TLV_TAG_TIME_SYNC_REQUEST:
    case TLV_TAG_TIME_REPLY:
    case TLV_TAG_ACK:
    case TLV_TAG_NACK:
        return COMM_PRIO_MED;

    case TLV_TAG_DATA_REPORT:
    case TLV_TAG_QUERY_RECORD:
    case TLV_TAG_QUERY_END:
    default:
        return COMM_PRIO_LOW;
    }
}

/* Queues one outgoing TLV frame. Priority is derived from `tag` via
 * comm_priority_for_tag(); keep-alive uses overwrite-mailbox semantics
 * (a full txq_high means exactly one stale item, which gets replaced),
 * events/data-reports use plain drop-on-full. Non-blocking; rings
 * sem_tx_ready exactly once per item actually queued. */
int comm_send(Communication *comm, uint8_t tag,
              const uint8_t *value, uint8_t value_len)
{
    comm_tx_item_t item;
    comm_prio_t prio;
    osMessageQueueId_t target_q;
    osStatus_t status;

    if (comm == NULL)
    {
        return -1;
    }
    if (value == NULL && value_len > 0u)
    {
        return -1;
    }
    if (value_len > COMM_MAX_VALUE)
    {
        return -1;
    }

    item.tag = tag;
    item.len = value_len;
    if (value_len > 0u)
    {
        memcpy(item.value, value, value_len);
    }

    prio = comm_priority_for_tag(tag);
    switch (prio)
    {
    case COMM_PRIO_HIGH:
        target_q = comm->txq_high;
        break;
    case COMM_PRIO_MED:
        target_q = comm->txq_med;
        break;
    default:
        target_q = comm->txq_low;
        break;
    }

    status = osMessageQueuePut(target_q, &item, 0, 0);

    if (status != osOK && prio == COMM_PRIO_HIGH)
    {
        /* Mailbox full means exactly one stale keep-alive is sitting
         * there. It's worthless now that a fresher one exists -- replace
         * it rather than let the stale one be sent late. */
        comm_tx_item_t stale;
        (void)osMessageQueueGet(comm->txq_high, &stale, NULL, 0);
        status = osMessageQueuePut(target_q, &item, 0, 0);
    }

    if (status != osOK)
    {
        return -1; /* queue full (events/data), message dropped */
    }

    osSemaphoreRelease(comm->sem_tx_ready);
    return 0;
}

/* The only function that ever transmits. Runs forever on its own task:
 * sleeps on sem_tx_ready until comm_send() rings it, drains exactly one
 * item from the highest non-empty queue (high -> med -> low), encodes
 * it with tlv_encode(), sends it with a blocking HAL_UART_Transmit(),
 * then goes back to sleep. Never called directly -- launched via
 * osThreadNew() inside communication_create(). */
static void comm_tx_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        comm_tx_item_t item;
        uint8_t frame[TLV_MAX_FRAME];
        size_t frame_len;

        osSemaphoreAcquire(g_comm.sem_tx_ready, osWaitForever);

        if (osMessageQueueGet(g_comm.txq_high, &item, NULL, 0) != osOK)
        {
            if (osMessageQueueGet(g_comm.txq_med, &item, NULL, 0) != osOK)
            {
                osMessageQueueGet(g_comm.txq_low, &item, NULL, 0);
            }
        }

        if (tlv_encode(item.tag, item.value, item.len,
                       frame, sizeof(frame), &frame_len) == TLV_OK)
        {
            HAL_UART_Transmit(&huart2, frame, (uint16_t)frame_len, 50);
        }
    }
}

/* ===============================================================
 * RX side
 *
 * HAL_UART_RxCpltCallback() (and ..._ErrorCallback) do the minimum
 * possible in interrupt context: hand a byte to comm_rx_task and
 * re-arm. comm_rx_task() does the real work -- feeds the TLV streaming
 * decoder one byte at a time and routes finished frames by tag.
 * =============================================================== */

/* HAL calls this automatically once a byte finishes arriving (a weak
 * override of HAL's empty default -- see communication.h/Step 5 notes
 * for how that linking works). Minimum possible work: hand the byte to
 * comm_rx_task via rxq_bytes, then re-arm for the next byte. No
 * protocol logic runs here. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2)
    {
        return;
    }

    uint8_t b = g_comm.rx_isr_byte;
    (void)osMessageQueuePut(g_comm.rxq_bytes, &b, 0, 0);
    (void)HAL_UART_Receive_IT(&huart2, &g_comm.rx_isr_byte, 1);
}

/* HAL calls this on a framing/noise/overrun error. Just re-arms RX so
 * one line glitch doesn't permanently stop reception -- UART-level
 * recovery, not protocol logic. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2)
    {
        return;
    }
    (void)HAL_UART_Receive_IT(&huart2, &g_comm.rx_isr_byte, 1);
}

/* The NVIC vector (USART2_IRQHandler) is CubeMX-generated in
 * stm32l4xx_it.c and regenerates on every code-gen pass regardless of
 * what lives elsewhere; it just calls HAL_UART_IRQHandler(), which
 * dispatches to the callbacks above. Only the callbacks belong here. */

/* Empty placeholders so the firmware links before Configuration/Init/
 * Log exist. Each real module later defines the same-named non-weak
 * function to take over -- no edits needed here when that happens. */
__attribute__((weak)) void configuration_on_frame(const tlv_frame_t *f) { (void)f; }
__attribute__((weak)) void init_on_frame(const tlv_frame_t *f) { (void)f; }
__attribute__((weak)) void log_on_frame(const tlv_frame_t *f) { (void)f; }
__attribute__((weak)) void event_on_frame(const tlv_frame_t *f) { (void)f; }

/* Dispatches one decoded frame by tag to whichever module owns it. */
static void comm_route_frame(const tlv_frame_t *f)
{
    switch (f->tag)
    {
    case TLV_TAG_SET_TEMP_NORMAL:
    case TLV_TAG_SET_TEMP_WARNING:
    case TLV_TAG_SET_HUM_NORMAL:
    case TLV_TAG_SET_HUM_WARNING:
    case TLV_TAG_SET_LIGHT_NORMAL:
    case TLV_TAG_SET_LIGHT_WARNING:
    case TLV_TAG_SET_BATT_NORMAL:
    case TLV_TAG_SET_BATT_WARNING:
    case TLV_TAG_GET_CONFIG: /* CC asking for the current thresholds -- Configuration owns replying */
        configuration_on_frame(f);
        break;

    /* GET_TIME: CC asking for the LNC's current time -- Init owns
     * replying (it owns RTC concerns), not Configuration.
     * TIME_SYNC_REPLY: the CC's answer to Init's own boot-time
     * TIME_SYNC_REQUEST. Both are Init's. */
    case TLV_TAG_GET_TIME:
    case TLV_TAG_TIME_SYNC_REPLY:
        init_on_frame(f);
        break;

    /* QUERY_DATA: measurements in a time range -- Log owns the stored
     * LOG1..7.TXT files. QUERY_EVENTS: same idea for EVENTS.TXT, owned
     * by Event. Each replies with zero or more TLV_TAG_QUERY_RECORD
     * frames followed by one TLV_TAG_QUERY_END. */
    case TLV_TAG_QUERY_DATA:
        log_on_frame(f);
        break;

    case TLV_TAG_QUERY_EVENTS:
        event_on_frame(f);
        break;

    default:
        break;
    }
}

/* The only function that ever reads huart2's incoming bytes. Runs
 * forever on its own task: sleeps on rxq_bytes until the ISR delivers
 * a byte, feeds it to the TLV streaming decoder, and routes whatever
 * frame comes out. Never called directly -- launched via osThreadNew()
 * inside communication_create(). */
static void comm_rx_task(void *argument)
{
    (void)argument;

    tlv_receiver_init(&g_comm.rx_recv);
    (void)HAL_UART_Receive_IT(&huart2, &g_comm.rx_isr_byte, 1);

    for (;;)
    {
        uint8_t byte;

        if (osMessageQueueGet(g_comm.rxq_bytes, &byte, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        tlv_frame_t frame;
        tlv_status_t st = tlv_receiver_feed_byte(&g_comm.rx_recv, byte, &frame);
        if (st == TLV_OK)
        {
            comm_route_frame(&frame);
        }
        /* TLV_INCOMPLETE: keep going. TLV_ERR_CRC: bad frame, already dropped. */
    }
}
