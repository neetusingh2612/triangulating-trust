/* ==========================================================================
 * can_hal.h -- the ONE board-specific seam.
 *
 * Everything else in this firmware is portable. To move to a different MCU
 * (S32K, SAM, etc.) you reimplement exactly these five functions in a new
 * can_hal_<board>.c and change nothing else.
 *
 * A reference implementation for STM32F103 (bxCAN + TJA1050) is provided in
 * can_hal_stm32f103.c.
 * ========================================================================== */
#ifndef CAN_HAL_H
#define CAN_HAL_H

#include <stdint.h>

typedef struct {
    uint32_t id;        /* 11-bit standard ID */
    uint8_t  dlc;       /* 0..8 */
    uint8_t  data[8];
} can_frame_t;

/* Bring up the CAN peripheral + transceiver at `bitrate_bps` (use 500000 to
 * match the ROAD dataset). Returns 0 on success. Also starts the free-running
 * cycle counter (DWT->CYCCNT on ARMv7-M). */
int      can_hal_init(uint32_t core_hz, uint32_t bitrate_bps);

/* Loopback self-test (LPB mode). Returns 0 if a frame loops back. RUN FIRST. */
int      can_hal_selftest(void);

/* Blocking transmit of one frame into a free TX mailbox. Returns 0 on success,
 * <0 on timeout. */
int      can_hal_send(const can_frame_t *f);

/* Non-blocking receive. Returns 1 and fills *f if a frame was waiting, else 0. */
int      can_hal_recv(can_frame_t *f);

/* Peripheral error/dropped-frame counters, read straight from the CAN core
 * (e.g. bxCAN ESR/error counters + our own RX-overrun tally). Used for the
 * "frames dropped" measurement (scaffold slots 26-27). */
uint32_t can_hal_tx_errors(void);
uint32_t can_hal_rx_overruns(void);

/* --- portable cycle counter (ARMv7-M DWT). Defined in can_hal_<board>.c
 * because enabling DWT is core/debug-unit specific on some parts. --- */
uint32_t cyc_now(void);          /* current DWT->CYCCNT */
void     cyc_init(void);         /* enable trace + reset counter */

/* Millisecond busy-wait, for load-test pacing. */
void     delay_ms(uint32_t ms);

#endif /* CAN_HAL_H */
