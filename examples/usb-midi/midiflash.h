#include <mchck.h>

#include <usb/usb.h>
#include <usb/usbmidi.h>

enum midi_rx_state
{
        MRS_WAIT,
        MRS_CMD_11,
        MRS_CMD_21,
        MRS_CMD_22,
        MRS_SYSEX0,
        MRS_SYSEX1,
        MRS_SYSEX2,
};

struct midi_rx_context;

typedef void (*midi_message_cb)(struct midi_rx_context *ctx, int len, int is_sysex);

struct midi_rx_context
{
        enum midi_rx_state state:8;
        uint8_t buffer[3];
        midi_message_cb message_cb;
        void *cbdata;
};

extern void
midi_rx_context_init(struct midi_rx_context *ctx, midi_message_cb cb, void *cbdata);

extern void
midi_rx_context_send_byte(struct midi_rx_context *ctx, uint8_t byte);

extern void
uart_tx_unblocked(void *cbdata, unsigned space);

/********************************************************************/

extern void
midi_uart_init(midi_message_cb cb, void *cbdata);

extern int
midi_uart_flow_control(void);

extern void
midi_uart_send_byte(uint8_t byte);

/********************************************************************/

enum SequencerMode
{
        PM_NONE,
        PM_ERASE,
        PM_RECORD,
        PM_PLAY_TO_MIDI,
        PM_PLAY_TO_USB,
        PM_PLAY_TO_BOTH,
        PM_SKIP_FORWARD,
        PM_TRANSFER,
        PM_INIT,
        PM_FIND_END,
        PM_NO_FLASH,
        PM_FLASH_FULL,
};

extern void
seq_request_mode(enum SequencerMode new_mode, int playback_pos);

extern void
seq_send_status(void);

extern void
seq_record_to_flash(struct midi_rx_context *ctx, int bytes, int is_sysex);

extern void
seq_init(void);

extern void
seq_set_speed(uint8_t speed);

/********************************************************************/

enum {
        SYSEX_CMD_RESET = 1,
        SYSEX_CMD_PLAY = 2,
        SYSEX_CMD_STOP = 3,
        SYSEX_CMD_ERASE = 4,
        SYSEX_CMD_ERASE_RESPONSE = 0x44,
        SYSEX_CMD_STATUS = 5,
        SYSEX_CMD_STATUS_RESPONSE = 0x45,
        SYSEX_CMD_SKIP = 6,
        SYSEX_CMD_SKIP_RESPONSE = 0x46,
        SYSEX_CMD_TRANSFER = 7,
        SYSEX_CMD_TRANSFER_RESPONSE = 0x47,
        SYSEX_CMD_SET_SPEED = 8,
};

/********************************************************************/

extern void
usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2);

extern void
send_from_midi_context_to_usb(struct midi_rx_context *ctx, int bytes, int is_sysex);

