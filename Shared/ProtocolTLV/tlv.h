/*
 * tlv.h - Tag / Length / Value message protocol
 *
 * Submarine Monitoring System.
 * Shared by the LNC firmware (STM32, ARM GCC) and the Linux side
 * (Central Computer and Ground Station, both C++).
 *
 * Plain C99. No malloc. No stdio. No floating point.
 * One copy of this file exists in the repo, so the two sides
 * cannot drift apart.
 *
 * ---------------------------------------------------------------
 * Frame on the wire
 * ---------------------------------------------------------------
 *
 *   +------+------+------+------+-----------+--------+--------+
 *   | SOF0 | SOF1 | TAG  | LEN  |   VALUE   | CRC hi | CRC lo |
 *   | 0xA5 | 0x5A |  u8  |  u8  |  LEN byt. |        |        |
 *   +------+------+------+------+-----------+--------+--------+
 *      1      1      1      1      0..255       1        1
 *
 *   Smallest frame:   6 bytes  (empty value)
 *   Largest frame:  261 bytes
 *
 *   CRC covers TAG, LEN and VALUE. Not the sync bytes.
 *   All multi-byte fields are big-endian (most significant byte first).
 *
 * ---------------------------------------------------------------
 * Why two sync bytes and not one
 * ---------------------------------------------------------------
 *
 *   With a single marker byte, any 0xA5 in line noise has a 1 in
 *   256 chance of looking like the start of a frame. The byte
 *   after it is then read as a length. A bogus length of 200-odd
 *   makes the parser sit and swallow the next real message.
 *
 *   Two bytes, 0xA5 then 0x5A, drop that to 1 in 65536. The pair
 *   is deliberately bit-inverse of each other, so a line stuck
 *   high, stuck low, or repeating one value cannot fake it.
 *
 *   Cost: one extra byte per frame. On a keep-alive every 6 s
 *   that is nothing.
 */

#ifndef SMS_TLV_H
#define SMS_TLV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ---------------------------------------------------------------
     * Frame constants
     * --------------------------------------------------------------- */

#define TLV_SOF0 0xA5u
#define TLV_SOF1 0x5Au
#define TLV_HEADER_SIZE 4u /* SOF0 + SOF1 + TAG + LEN      */
#define TLV_CRC_SIZE 2u
#define TLV_MAX_VALUE 255u                                             /* LEN is one byte              */
#define TLV_MIN_FRAME (TLV_HEADER_SIZE + TLV_CRC_SIZE)                 /*   6 */
#define TLV_MAX_FRAME (TLV_HEADER_SIZE + TLV_MAX_VALUE + TLV_CRC_SIZE) /* 261 */

    /* ---------------------------------------------------------------
     * Tags
     *
     * Grouped by direction and purpose so the number tells you
     * where a message came from when you read a hex dump.
     *
     *   0x10..0x1F   LNC  -> Central Computer   (reports and events)
     *   0x20..0x2F   CC   -> LNC                (configuration)
     *   0x30..0x3F   CC   -> LNC                (queries)
     *   0x40..0x4F   LNC  -> CC                 (replies)
     *   0x50..0x5F   Ground Station <-> CC
     * --------------------------------------------------------------- */

    typedef enum
    {
        TLV_TAG_INVALID = 0x00,

        /* LNC -> Central Computer */
        TLV_TAG_DATA_REPORT = 0x10,     /* periodic measurements + mode      */
        TLV_TAG_MODE_CHANGE = 0x11,     /* Normal/Warning/Error transition   */
        TLV_TAG_OBJECT_DETECTED = 0x12, /* sonar / IR: object appeared       */
        TLV_TAG_OBJECT_CLEARED = 0x13,  /* sonar / IR: object gone           */
        TLV_TAG_KEEP_ALIVE = 0x14,      /* every 6 s, highest priority       */
        TLV_TAG_STARTUP = 0x15,         /* boot, says if it was a WD reset   */
        TLV_TAG_TIME_SYNC_REQUEST = 0x16, /* Init, at boot: "please sync me"
                                            * (section 2.7) -- distinct from
                                            * GET_TIME below, which flows the
                                            * other way. Answered by
                                            * TLV_TAG_TIME_SYNC_REPLY.       */

        /* Central Computer -> LNC : the eight threshold-setting commands,
         * plus GET_TIME/TIME_SYNC_REPLY (see below). No SET_TIME -- this
         * design deliberately never lets the CC push a time correction;
         * the only two time exchanges are TLV_TAG_TIME_SYNC_REQUEST
         * above (LNC asks CC, at boot) and TLV_TAG_GET_TIME below (CC
         * asks LNC, any time). */
        TLV_TAG_SET_TEMP_NORMAL = 0x20,   /* temperature range for Normal      */
        TLV_TAG_SET_TEMP_WARNING = 0x21,  /* temperature range for Warning     */
        TLV_TAG_SET_HUM_NORMAL = 0x22,    /* humidity lower bound, Normal      */
        TLV_TAG_SET_HUM_WARNING = 0x23,   /* humidity lower bound, Warning     */
        TLV_TAG_SET_LIGHT_NORMAL = 0x24,  /* light lower bound, Normal         */
        TLV_TAG_SET_LIGHT_WARNING = 0x25, /* light lower bound, Warning        */
        TLV_TAG_SET_BATT_NORMAL = 0x26,   /* battery lower bound, Normal       */
        TLV_TAG_SET_BATT_WARNING = 0x27,  /* battery lower bound, Warning      */
        TLV_TAG_GET_TIME = 0x28,          /* CC asks the LNC's current time,
                                            * e.g. to check it's synced --
                                            * answered by TLV_TAG_TIME_REPLY
                                            * below. Not Init's own sync
                                            * request -- see
                                            * TLV_TAG_TIME_SYNC_REQUEST
                                            * above for that.               */
        TLV_TAG_TIME_SYNC_REPLY = 0x29,   /* CC's answer to Init's
                                            * TLV_TAG_TIME_SYNC_REQUEST      */
        TLV_TAG_GET_CONFIG = 0x2A,       /* CC asks for the LNC's current
                                            * thresholds -- answered by
                                            * TLV_TAG_CONFIG_REPLY below.    */

        /* Central Computer -> LNC : queries */
        TLV_TAG_QUERY_DATA = 0x30,   /* measurements in a time range      */
        TLV_TAG_QUERY_EVENTS = 0x31, /* events in a time range            */

        /* LNC -> Central Computer : replies */
        TLV_TAG_TIME_REPLY = 0x40,   /* LNC's answer to CC's GET_TIME     */
        TLV_TAG_QUERY_RECORD = 0x41, /* one record of a query answer      */
        TLV_TAG_QUERY_END = 0x42,    /* no more records                   */
        TLV_TAG_ACK = 0x43,          /* command accepted                  */
        TLV_TAG_NACK = 0x44,         /* command rejected, reason in value */
        TLV_TAG_CONFIG_REPLY = 0x45, /* LNC's answer to CC's GET_CONFIG   */

        /* Ground Station <-> Central Computer */
        TLV_TAG_GS_QUERY_DATA = 0x50,
        TLV_TAG_GS_QUERY_EVENTS = 0x51,
        TLV_TAG_GS_RECORD = 0x52,
        TLV_TAG_GS_END = 0x53
    } tlv_tag_t;

    /* ---------------------------------------------------------------
     * Return codes
     *
     * Negative means failure, so `if (rc < 0)` is a valid check
     * and callers cannot confuse an error with a byte count.
     * --------------------------------------------------------------- */

    typedef enum
    {
        TLV_OK = 0,         /* done, or a whole frame is ready         */
        TLV_INCOMPLETE = 1, /* not an error: need more bytes           */
        TLV_ERR_NULL = -1,  /* a required pointer was NULL             */
        TLV_ERR_SPACE = -2, /* output buffer too small                 */
        /* Reserved. Cannot happen while value_len is a uint8_t: the type
         * itself makes an over-long value unrepresentable. Kept so the
         * code still reads correctly if LEN ever grows to two bytes. */
        TLV_ERR_TOO_LONG = -3,
        TLV_ERR_NO_SOF = -4, /* buffer does not start with A5 5A        */
        TLV_ERR_CRC = -5,    /* frame arrived corrupted                 */
        TLV_ERR_RANGE = -6   /* reader ran past the end of the payload  */
    } tlv_status_t;

    /* ---------------------------------------------------------------
     * A decoded frame
     *
     * `value` points into a buffer someone else owns. Nothing is
     * copied and nothing is allocated. It stays valid only until
     * that buffer is reused.
     * --------------------------------------------------------------- */

    /* One decoded TLV frame: tag, length, and a view into the value bytes. */
    typedef struct
    {
        uint8_t tag;         /* one of TLV_TAG_*                */
        uint8_t len;         /* number of bytes in value        */
        const uint8_t *value; /* NULL when len == 0              */
    } tlv_frame_t;

    /* ---------------------------------------------------------------
     * CRC-16/CCITT-FALSE   poly 0x1021, init 0xFFFF, no reflection
     * Check value for the ASCII string "123456789" is 0x29B1.
     * --------------------------------------------------------------- */

    /* Fold one byte into a running CRC; carry the return value into the next call. */
    uint16_t tlv_crc16_update(uint16_t crc, uint8_t byte);
    /* CRC over a whole buffer at once (loops tlv_crc16_update() internally). */
    uint16_t tlv_crc16(const uint8_t *data, size_t len);

    /* ---------------------------------------------------------------
     * Encode
     *
     * Writes one complete frame into `out`.
     * `value` may be NULL only when `value_len` is 0.
     * On success `*out_len` holds the number of bytes written.
     * --------------------------------------------------------------- */

    tlv_status_t tlv_encode(uint8_t tag,
                            const uint8_t *value,
                            uint8_t value_len,
                            uint8_t *out,
                            size_t out_cap,
                            size_t *out_len);

    /* ---------------------------------------------------------------
     * Decode one frame from a buffer that already holds whole frames.
     *
     * Used by unit tests and by the mock transport. Real serial input
     * arrives in pieces, so use the streaming parser below for that.
     *
     * `in` must start on the first sync byte.
     * On success `*consumed` holds the frame size, so the caller can
     * step forward and decode the next one.
     * --------------------------------------------------------------- */

    tlv_status_t tlv_decode(const uint8_t *in,
                            size_t in_len,
                            tlv_frame_t *frame,
                            size_t *consumed);

    /* ---------------------------------------------------------------
     * Streaming parser  ==  tlv_decode, one byte at a time
     *
     * This does exactly the same decoding as tlv_decode above: check
     * the marker, read the tag, read the length, collect the value,
     * verify the CRC. The difference is that tlv_decode needs the whole
     * frame in memory before it can start, and serial input never
     * arrives that way - read() returns 5 bytes, then 5, then 4.
     *
     * So this is the one real receiving code calls. tlv_decode is for
     * tests and for transports that hand you a complete block.
     *
     * Feed it whatever read() gave you, one byte or two hundred.
     * It finds the frame boundaries itself and recovers from garbage.
     *
     * Constant work per byte: one compare and, inside a frame, one
     * incremental CRC step. It never re-scans what it already read.
     * That matters on the MCU, where an input-dependent spike in CPU
     * time would show up as a missed watchdog refresh.
     *
     * The trade-off: if a frame is corrupted, the parser throws away
     * the rest of that frame and hunts for the next sync pair. A real
     * message that began inside the damaged stretch is lost. With a
     * two-byte sync word that is a rare case on top of a rare case,
     * and the next keep-alive arrives within 6 s.
     *
     * The struct is ~280 bytes and is meant to live as a static or a
     * member. Do not put it on a small task stack.
     * --------------------------------------------------------------- */

    /* State for the streaming (byte-at-a-time) receiver, plus running
     * diagnostic counters. Lives across calls to tlv_receiver_feed_byte(). */
    typedef struct
    {
        uint8_t state;    /* which byte of the frame is expected next */
        uint8_t tag;      /* tag of the frame being assembled         */
        uint8_t len;      /* declared length of that frame's value    */
        uint8_t idx;      /* value bytes collected so far             */
        uint16_t crc_calc; /* CRC computed over bytes seen so far      */
        uint16_t crc_rx;   /* CRC read from the wire, for comparison   */
        uint8_t value[TLV_MAX_VALUE]; /* value bytes collected so far  */

        /* counters, for diagnostics and for the sermon tool */
        uint32_t frames_ok;
        uint32_t crc_errors;
        uint32_t bytes_dropped;
    } tlv_receiver_t;

    /* Reset a receiver to a blank state, ready to hunt for a new frame. */
    void tlv_receiver_init(tlv_receiver_t *recv);

    /*
     * Feed one byte.
     *   TLV_OK         a complete, CRC-checked frame is in *out
     *   TLV_INCOMPLETE keep going
     *   TLV_ERR_CRC    a frame arrived damaged and was thrown away
     */
    tlv_status_t tlv_receiver_feed_byte(tlv_receiver_t *recv, uint8_t byte, tlv_frame_t *out);

    /* Callback form, for draining a whole read() buffer in one call.
     * Returns how many good frames were delivered. */
    typedef void (*tlv_frame_cb)(const tlv_frame_t *frame, void *ctx);

    /* Feed a whole buffer; calls cb() once per good frame found. Returns how many. */
    size_t tlv_receiver_feed(tlv_receiver_t *recv,
                       const uint8_t *data,
                       size_t len,
                       tlv_frame_cb cb,
                       void *ctx);

    /* ---------------------------------------------------------------
     * Payload writer / reader
     *
     * These build and take apart the VALUE field. They exist so that
     * no one is ever tempted to memcpy a struct onto the wire, which
     * would break the moment padding or endianness differed between
     * the ARM build and the x86 build.
     *
     * Both carry a sticky `ok` flag: write ten fields, check once at
     * the end. An error early on makes every later call a no-op, so
     * there is no way to read past the buffer.
     * --------------------------------------------------------------- */

    /* Appends fields into a payload buffer, most significant byte first,
     * with a sticky ok flag so overflow can be checked once at the end. */
    typedef struct
    {
        uint8_t *buf; /* destination buffer          */
        uint16_t cap; /* buffer capacity, in bytes    */
        uint16_t len; /* bytes written so far         */
        int ok;       /* 1 while every write has fit  */
    } tlv_writer_t;

    /* Point a writer at an output buffer and its capacity; resets the write cursor. */
    void tlv_writer_init(tlv_writer_t *writer, uint8_t *buf, uint16_t cap);
    /* Append one byte. */
    void tlv_writer_put_u8(tlv_writer_t *writer, uint8_t value);
    /* Append a 16-bit value, most significant byte first. */
    void tlv_writer_put_u16(tlv_writer_t *writer, uint16_t value);
    /* Append a 32-bit value, most significant byte first. */
    void tlv_writer_put_u32(tlv_writer_t *writer, uint32_t value);
    /* Signed tlv_writer_put_u16(). */
    void tlv_writer_put_i16(tlv_writer_t *writer, int16_t value);
    /* Signed tlv_writer_put_u32(). */
    void tlv_writer_put_i32(tlv_writer_t *writer, int32_t value);
    /* Append a raw byte array as-is. */
    void tlv_writer_put_bytes(tlv_writer_t *writer, const uint8_t *src, uint16_t n);
    int tlv_writer_ok(const tlv_writer_t *writer); /* 1 = every write fitted */

    /* Reads fields back out of a payload buffer, most significant byte
     * first, with a sticky ok flag so underrun can be checked once at the end. */
    typedef struct
    {
        const uint8_t *buf; /* source buffer                    */
        uint16_t len;       /* buffer length, in bytes          */
        uint16_t pos;       /* read cursor                      */
        int ok;             /* 1 while no read has run past the end */
    } tlv_reader_t;

    /* Point a reader at a received payload buffer; resets the read cursor. */
    void tlv_reader_init(tlv_reader_t *reader, const uint8_t *buf, uint16_t len);
    /* Read one byte. */
    uint8_t tlv_reader_get_u8(tlv_reader_t *reader);
    /* Read a 16-bit value, most significant byte first. */
    uint16_t tlv_reader_get_u16(tlv_reader_t *reader);
    /* Read a 32-bit value, most significant byte first. */
    uint32_t tlv_reader_get_u32(tlv_reader_t *reader);
    /* Signed tlv_reader_get_u16(). */
    int16_t tlv_reader_get_i16(tlv_reader_t *reader);
    /* Signed tlv_reader_get_u32(). */
    int32_t tlv_reader_get_i32(tlv_reader_t *reader);
    /* Read n raw bytes into dst. */
    void tlv_reader_get_bytes(tlv_reader_t *reader, uint8_t *dst, uint16_t n);
    int tlv_reader_ok(const tlv_reader_t *reader);   /* 1 = no overrun    */
    int tlv_reader_done(const tlv_reader_t *reader); /* 1 = ok and empty  */

#ifdef __cplusplus
}
#endif

#endif /* SMS_TLV_H */