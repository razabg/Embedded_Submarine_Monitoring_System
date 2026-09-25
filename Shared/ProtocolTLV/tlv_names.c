/*
 * tlv_names.c - human readable tag names.
 *
 * Deliberately a separate file. Only the Central Computer and the
 * Ground Station compile it. The firmware does not, so these
 * strings never take up flash on the MCU.
 *
 * This is why tlv.c has no strings in it at all.
 */

#include "tlv_names.h"

const char *tlv_tag_name(uint8_t tag)
{
    switch (tag)
    {
    case TLV_TAG_DATA_REPORT:
        return "DATA_REPORT";
    case TLV_TAG_MODE_CHANGE:
        return "MODE_CHANGE";
    case TLV_TAG_OBJECT_DETECTED:
        return "OBJECT_DETECTED";
    case TLV_TAG_OBJECT_CLEARED:
        return "OBJECT_CLEARED";
    case TLV_TAG_KEEP_ALIVE:
        return "KEEP_ALIVE";
    case TLV_TAG_STARTUP:
        return "STARTUP";
    case TLV_TAG_TIME_SYNC_REQUEST:
        return "TIME_SYNC_REQUEST";

    case TLV_TAG_SET_TEMP_NORMAL:
        return "SET_TEMP_NORMAL";
    case TLV_TAG_SET_TEMP_WARNING:
        return "SET_TEMP_WARNING";
    case TLV_TAG_SET_HUM_NORMAL:
        return "SET_HUM_NORMAL";
    case TLV_TAG_SET_HUM_WARNING:
        return "SET_HUM_WARNING";
    case TLV_TAG_SET_LIGHT_NORMAL:
        return "SET_LIGHT_NORMAL";
    case TLV_TAG_SET_LIGHT_WARNING:
        return "SET_LIGHT_WARNING";
    case TLV_TAG_SET_BATT_NORMAL:
        return "SET_BATT_NORMAL";
    case TLV_TAG_SET_BATT_WARNING:
        return "SET_BATT_WARNING";
    case TLV_TAG_GET_TIME:
        return "GET_TIME";
    case TLV_TAG_TIME_SYNC_REPLY:
        return "TIME_SYNC_REPLY";
    case TLV_TAG_GET_CONFIG:
        return "GET_CONFIG";

    case TLV_TAG_QUERY_DATA:
        return "QUERY_DATA";
    case TLV_TAG_QUERY_EVENTS:
        return "QUERY_EVENTS";

    case TLV_TAG_TIME_REPLY:
        return "TIME_REPLY";
    case TLV_TAG_QUERY_RECORD:
        return "QUERY_RECORD";
    case TLV_TAG_QUERY_END:
        return "QUERY_END";
    case TLV_TAG_ACK:
        return "ACK";
    case TLV_TAG_NACK:
        return "NACK";
    case TLV_TAG_CONFIG_REPLY:
        return "CONFIG_REPLY";

    case TLV_TAG_GS_QUERY_DATA:
        return "GS_QUERY_DATA";
    case TLV_TAG_GS_QUERY_EVENTS:
        return "GS_QUERY_EVENTS";
    case TLV_TAG_GS_RECORD:
        return "GS_RECORD";
    case TLV_TAG_GS_END:
        return "GS_END";

    default:
        return "UNKNOWN";
    }
}

const char *tlv_status_name(tlv_status_t st)
{
    switch (st)
    {
    case TLV_OK:
        return "OK";
    case TLV_INCOMPLETE:
        return "INCOMPLETE";
    case TLV_ERR_NULL:
        return "ERR_NULL";
    case TLV_ERR_SPACE:
        return "ERR_SPACE";
    case TLV_ERR_TOO_LONG:
        return "ERR_TOO_LONG";
    case TLV_ERR_NO_SOF:
        return "ERR_NO_SOF";
    case TLV_ERR_CRC:
        return "ERR_CRC";
    case TLV_ERR_RANGE:
        return "ERR_RANGE";
    default:
        return "ERR_UNKNOWN";
    }
}