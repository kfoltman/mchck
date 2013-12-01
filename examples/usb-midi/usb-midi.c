// 1. compress start-of-song events before first note
// 2. find out why next song playback is not working / rewrite the code
// 3. implement previous song
// 4. change to sysex format (mostly done)
// 5. add status feedback as sysex (mostly done)
// 6. write a client (partially done)
// 7. add SMF export
// 8. add SMF import
#include <mchck.h>

#include "midiflash.h"

#define DEBUG 1

/****************************************************************************/

static struct usbmidi_ctx umctx;

static int enabled = 0;

/****************************************************************************/

void
usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2);

extern void
uart_tx_unblocked(void *cbdata, unsigned space)
{
        usbmidi_read_more(&umctx);
}

/****************************************************************************
 * Timer handling
 ****************************************************************************/

/****************************************************************************
 * Flash handling
 ****************************************************************************/

static volatile enum SequencerMode seq_mode = PM_RECORD;
static volatile enum SequencerMode next_state_request = PM_NONE;

/****************************************************************************/

inline void
usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        if (!enabled)
                return;
        usbmidi_tx(&umctx, addr_type, ctl, data1, data2);
}

void
send_from_midi_context_to_usb(struct midi_rx_context *ctx, int bytes, int is_sysex)
{
        if (is_sysex)
        {
                usb_midi_send(is_sysex == 1 ? 4 : 4 + bytes, ctx->buffer[0], ctx->buffer[1], ctx->buffer[2]);
                return;
        }
        uint8_t cmd = ctx->buffer[0];
        switch(bytes)
        {
        case 1:
                usb_midi_send(cmd >> 4, cmd, 0, 0);
                break;
        case 2:
                usb_midi_send(cmd < 0xF0 ? (cmd >> 4) : 2, cmd, ctx->buffer[1], 0);
                break;
        case 3:
                usb_midi_send(cmd < 0xF0 ? (cmd >> 4) : 3, cmd, ctx->buffer[1], ctx->buffer[2]);
                break;
        default:
                break;
        }        
}

/****************************************************************************/

static int sysex_state = 0;
static uint8_t sysex_buffer[16];

static void
handle_sysex_end(void)
{
        switch(sysex_buffer[0])
        {
        case SYSEX_CMD_RESET:
                RFVBAT_REG7 |= 1;
                sys_reset();
                return;
        case SYSEX_CMD_PLAY:
                if (sysex_state == 2)
                {
                        int pm = sysex_buffer[1];
                        if (pm == PM_PLAY_TO_MIDI || pm == PM_PLAY_TO_USB || pm == PM_PLAY_TO_BOTH)
                                seq_request_mode(pm, -1);
                }
                if (sysex_state == 5)
                {
                        int pm = sysex_buffer[1];
                        if (pm == PM_PLAY_TO_MIDI || pm == PM_PLAY_TO_USB || pm == PM_PLAY_TO_BOTH)
                                seq_request_mode(pm, sysex_buffer[2] + 128 * sysex_buffer[3] + 128 * 128 * sysex_buffer[4]);
                }
                break;                
        case SYSEX_CMD_STOP:
                seq_request_mode(PM_RECORD, -1);
                break;
        case SYSEX_CMD_ERASE:
                if (sysex_state == 3 && sysex_buffer[1] == 0x55 && sysex_buffer[2] == 0x2A)
                        seq_request_mode(PM_ERASE, -1);
                break;                
        case SYSEX_CMD_STATUS:
                seq_send_status();
                break;
        case SYSEX_CMD_SKIP:
                seq_request_mode(PM_SKIP_FORWARD, -1);
                break;
        case SYSEX_CMD_TRANSFER:
                if (sysex_state == 4)
                {
                        seq_request_mode(PM_TRANSFER, sysex_buffer[1] + 128 * sysex_buffer[2] + 128 * 128 * sysex_buffer[3]);
                        return;
                }
                break;                
        }
        sysex_state = 0;
}

static
void handle_sysex_byte(uint8_t byte)
{
        if (sysex_state >= 16)
                return;
        sysex_buffer[sysex_state++] = byte;
}

/****************************************************************************/

int
handle_usb_midi_msg(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        onboard_led(-1);
        if (ctl == 0xF0 && data1 == 0x47) {
                sysex_state = 1;
                sysex_buffer[0] = data2;
                onboard_led(-1);
                return midi_uart_flow_control();
        }
        if (sysex_state)
        {
                if (ctl == 0xF7)
                        handle_sysex_end();
                else
                {
                        handle_sysex_byte(ctl);
                        if (data1 == 0xF7)
                                handle_sysex_end();
                        else
                        {
                                handle_sysex_byte(data1);
                                if (data2 == 0xF7)
                                        handle_sysex_end();
                                else
                                        handle_sysex_byte(data2);
                        }
                }
                onboard_led(-1);
                return midi_uart_flow_control();
        }                
        
        if (addr_and_type >= 2 && addr_and_type <= 7) {
                midi_uart_send_byte(ctl);
                if (addr_and_type != 5)
                        midi_uart_send_byte(data1);
                if (addr_and_type == 3 || addr_and_type == 4 || addr_and_type == 7)
                        midi_uart_send_byte(data2);
        }
        else {
                if (ctl >= 0x80)
                {
                        int bytes = usbmidi_bytes_from_ctl(ctl);
                        if (bytes >= 1)
                        {
                                midi_uart_send_byte(ctl);
                                if (bytes >= 2)
                                        midi_uart_send_byte(data1);
                                if (bytes >= 3)
                                        midi_uart_send_byte(data2);
                        }
                }
        }
        onboard_led(-1);
        return midi_uart_flow_control();
}

void
on_uart_midi_received(struct midi_rx_context *ctx, int bytes, int is_sysex)
{
        seq_record_to_flash(ctx, bytes, is_sysex);
        send_from_midi_context_to_usb(ctx, bytes, is_sysex);
}

/****************************************************************************/

static void
init_usbmidi(int config)
{
        usbmidi_init(&umctx, handle_usb_midi_msg, NULL);
        enabled = 1;
}

const struct usbd_device usbmidi_device = 
        USB_INIT_DEVICE(0x2323,              /* vid */
                        5,                   /* pid */
                        u"mchck.org",        /* vendor */
                        u"mchck recording MIDI adapter", /* product" */
                        (init_usbmidi,       /* init */
                         USBMIDI)           /* functions */
                );

void
main(void)
{
#ifdef DEBUG
        if (RFVBAT_REG7 & 1)
        {
            RFVBAT_REG7 &= ~1;
            sys_jump_to_programmer();
        }
#endif

        usb_init(&usbmidi_device);

        midi_uart_init(on_uart_midi_received, NULL);
        
        seq_init();
        
        sys_yield_for_frogs();
}
