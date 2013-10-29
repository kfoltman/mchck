#include <mchck.h>

#include <usb/usb.h>
#include <usb/usbmidi.h>

static struct usbmidi_ctx umctx;

int enabled = 0;

static struct fifo_ctx uart_tx_fifo;

struct uart_ctx uart0_ctx;

void
uart_tx_unblocked(void *cbdata, unsigned space)
{
        usbmidi_read_more(&umctx);
}

int
new_data(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        onboard_led(-1);
        if (addr_and_type >= 2 && addr_and_type <= 7) {
                uart_tx(&uart0_ctx, ctl);
                if (addr_and_type != 5)
                        uart_tx(&uart0_ctx, data1);
                if (addr_and_type == 3 || addr_and_type == 4 || addr_and_type == 7)
                        uart_tx(&uart0_ctx, data2);
        }
        else {
#ifdef DEBUG
                if (ctl == 0xF9) {
                        RFVBAT_REG7 |= 1;
                        sys_reset();
                }
#endif
                if (ctl >= 0x80)
                {
                        int bytes = usbmidi_bytes_from_ctl(ctl);
                        if (bytes >= 1)
                        {
                                uart_tx(&uart0_ctx, ctl);
                                if (bytes >= 2)
                                        uart_tx(&uart0_ctx, data1);
                                if (bytes >= 3)
                                        uart_tx(&uart0_ctx, data2);
                        }
                }
        }
        onboard_led(-1);
        return fifo_flow_control(&uart_tx_fifo, 6, uart_tx_unblocked, 0);
}

static void
init_usbmidi(int config)
{
        usbmidi_init(&umctx, new_data, NULL);
        enabled = 1;
}

const struct usbd_device usbmidi_device = 
        USB_INIT_DEVICE(0x2323,              /* vid */
                        5,                   /* pid */
                        u"mchck.org",        /* vendor */
                        u"USB MIDI adapter", /* product" */
                        (init_usbmidi,       /* init */
                         USBMIDI)           /* functions */
                );

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

struct midi_rx
{
        enum midi_rx_state state:8;
        uint8_t buffer[3];
} mrx;

static inline void usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        if (!enabled)
                return;
        usbmidi_tx(&umctx, addr_type, ctl, data1, data2);
}

static void uart_rx_handler(void *cbdata, uint8_t value)
{
        onboard_led(-1);
        uint8_t *buffer = mrx.buffer;
        if (mrx.state == MRS_SYSEX0 || mrx.state == MRS_SYSEX1 || mrx.state == MRS_SYSEX2)
        {
                buffer[mrx.state - MRS_SYSEX0] = value;
                if (value == 0xF7) {
                        usb_midi_send(5 + mrx.state - MRS_SYSEX0, buffer[0], buffer[1], buffer[2]);
                        mrx.state = MRS_WAIT;
                }
                else {
                        mrx.state = mrx.state < MRS_SYSEX2 ? mrx.state + 1 : MRS_SYSEX0;
                        if (mrx.state == MRS_SYSEX0) {
                                usb_midi_send(4, buffer[0], buffer[1], buffer[2]);
                                memset(buffer, 0, 3);
                        }
                }
                onboard_led(-1);
                return;
        }
        if (value <= 0x80)
        {
                switch(mrx.state)
                {
                case MRS_WAIT:
                        break;
                case MRS_CMD_11:
                        if (buffer[0] < 0xF0)
                                usb_midi_send(buffer[0] >> 4, buffer[0], value, 0);
                        else
                                usb_midi_send(2, buffer[0], value, 0);
                        break;
                case MRS_CMD_21:
                        buffer[1] = value;
                        mrx.state = MRS_CMD_22;
                        break;
                case MRS_CMD_22:
                        if (buffer[0] < 0xF0)
                                usb_midi_send(buffer[0] >> 4, buffer[0], buffer[1], value);
                        else
                                usb_midi_send(3, buffer[0], buffer[1], value);
                        break;
                case MRS_SYSEX0:
                case MRS_SYSEX1:
                case MRS_SYSEX2:
                        break;
                }
                onboard_led(-1);
                return;
        }
        buffer[0] = value;
        int bytes = usbmidi_bytes_from_ctl(value);
        if (bytes == 3)
        {
                mrx.state = MRS_CMD_21;
        }
        else
        if (bytes == 2)
        {
                mrx.state = MRS_CMD_11;
        }
        else
        if (bytes == 1)
        {
                usb_midi_send(value >> 4, value, 0, 0);
        }
        else
        if (bytes == -1 && value == 0xF0)
        {
                mrx.state = MRS_SYSEX1;
                buffer[0] = 0xF0;
                // usb_midi_send(value >> 4, value, 0, 0);
        }
        onboard_led(-1);
}

uint8_t uart_tx_fifo_buffer[32];

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
        fifo_init(&uart_tx_fifo, uart_tx_fifo_buffer, sizeof(uart_tx_fifo_buffer));

        uart_init(&uart0_ctx, UART_CH0);
        uart_set_baud_rate(&uart0_ctx, 31250);
        uart_tx_enable(&uart0_ctx, &uart_tx_fifo);
        uart_tx_set_invert(&uart0_ctx, 1);
        
        usb_init(&usbmidi_device);
        
        uart_rx_enable(&uart0_ctx, uart_rx_handler, NULL);
        sys_yield_for_frogs();
}
