#ifndef __QOS_H__
#define __QOS_H__

#define APP_FLOWS_MAX       4

/**
 * Single-Rate Three Color Meter, blind mode
 */

enum qos_color {
    GREEN = 0,
    YELLOW,
    RED
};

/* Init meter */
int qos_meter_init(void);

/* Meter pkt, return color*/
enum qos_color qos_meter_run(uint32_t flow_id, uint32_t pkt_len, uint64_t time);



/**
 * Weighted Random Early Detection, weighted by Color
 */

/* Init dropper */
int qos_dropper_init(void);


/* Make drop decision, 1-drop, 0-pass */
int qos_dropper_run(uint32_t flow_id, enum qos_color color, uint64_t time);


#endif