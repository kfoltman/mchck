#include "midiflash.h"

struct midi_rx_context uart_midi_rx_context;
struct fifo_ctx uart_tx_fifo;
struct uart_ctx uart0_ctx;
static uint8_t uart_tx_fifo_buffer[32];

static void uart_rx_handler(void *cbdata, uint8_t value)
{
        onboard_led(-1);
        midi_rx_context_send_byte(&uart_midi_rx_context, value);
        onboard_led(-1);
}

void
midi_uart_init(midi_message_cb cb, void *cbdata)
{
        fifo_init(&uart_tx_fifo, uart_tx_fifo_buffer, sizeof(uart_tx_fifo_buffer));

        uart_init(&uart0_ctx, UART_CH0);
        uart_set_baud_rate(&uart0_ctx, 31250);
        uart_tx_enable(&uart0_ctx, &uart_tx_fifo);
        uart_tx_set_invert(&uart0_ctx, 1);
        
        midi_rx_context_init(&uart_midi_rx_context, cb, cbdata);
        uart_rx_enable(&uart0_ctx, uart_rx_handler, NULL);
}

int
midi_uart_flow_control(void)
{
        return fifo_flow_control(&uart_tx_fifo, 6, uart_tx_unblocked, 0);
}

void
midi_uart_send_byte(uint8_t byte)
{
        uart_tx(&uart0_ctx, byte);
}

/******************************************************************************/

void
midi_rx_context_init(struct midi_rx_context *ctx, midi_message_cb cb, void *cbdata)
{
        ctx->state = MRS_WAIT;
        ctx->message_cb = cb;
        ctx->cbdata = cbdata;
}

void
midi_rx_context_send_byte(struct midi_rx_context *ctx, uint8_t byte)
{        
        uint8_t *buffer = ctx->buffer;
        if (ctx->state == MRS_SYSEX0 || ctx->state == MRS_SYSEX1 || ctx->state == MRS_SYSEX2)
        {
                buffer[ctx->state - MRS_SYSEX0] = byte;
                if (byte == 0xF7) {
                        ctx->message_cb(ctx, 1 + ctx->state - MRS_SYSEX0, 2);
                        ctx->state = MRS_WAIT;
                }
                else {
                        ctx->state = ctx->state < MRS_SYSEX2 ? ctx->state + 1 : MRS_SYSEX0;
                        if (ctx->state == MRS_SYSEX0) {
                                ctx->message_cb(ctx, 3, 1);
                                memset(buffer, 0, 3);
                        }
                }
                return;
        }
        if (byte <= 0x80)
        {
                switch(ctx->state)
                {
                case MRS_WAIT:
                        break;
                case MRS_CMD_11:
                        buffer[1] = byte;
                        ctx->message_cb(ctx, 2, 0);
                        break;
                case MRS_CMD_21:
                        buffer[1] = byte;
                        ctx->state = MRS_CMD_22;
                        break;
                case MRS_CMD_22:
                        buffer[2] = byte;
                        ctx->message_cb(ctx, 3, 0);
                        break;
                case MRS_SYSEX0:
                case MRS_SYSEX1:
                case MRS_SYSEX2:
                        break;
                }
                return;
        }
        buffer[0] = byte;
        int bytes = usbmidi_bytes_from_ctl(byte);
        if (bytes == 3)
                ctx->state = MRS_CMD_21;
        else if (bytes == 2)
                ctx->state = MRS_CMD_11;
        else if (bytes == 1)
                ctx->message_cb(ctx, 1, 0);
        else
        if (bytes == -1 && byte == 0xF0)
        {
                ctx->state = MRS_SYSEX1;
                buffer[0] = 0xF0;
        }
}

