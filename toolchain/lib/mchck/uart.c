#include <mchck.h>

static struct uart_ctx *uart_contexts[3];

void
uart_init(struct uart_ctx *ctx,
          enum UART_t port)
{
        switch(port) {
        case UART_0:
                SIM.scgc4.uart0 = 1;
                ctx->uart = UART0_BASE_PTR;
                int_enable(IRQ_UART0_status);
                break;
        case UART_1:
                SIM.scgc4.uart1 = 1;
                ctx->uart = UART1_BASE_PTR;
                int_enable(IRQ_UART1_status);
                break;
        case UART_2:
                SIM.scgc4.uart2 = 1;
                ctx->uart = UART2_BASE_PTR;
                int_enable(IRQ_UART2_status);
                break;
        }
        uart_contexts[port] = ctx;
        ctx->tx_fifo = NULL;
        ctx->rx_cb = NULL;
        ctx->rx_cbdata = NULL;
}

void
uart_set_baud_rate(struct uart_ctx *ctx, int baud_rate)
{
        int master_clock = 48000000;
        // XXXKF this is untested (see: pages 151 and 244 reference manual)
        if (ctx->uart == UART2_BASE_PTR)
                master_clock /= 1 + SIM.clkdiv1.outdiv2;
        baud_rate = ((master_clock << 2) / baud_rate + 1) >> 1;
        ctx->uart->C4 = baud_rate & 31;
        baud_rate = baud_rate >> 5;
        ctx->uart->BDH = baud_rate >> 8;
        ctx->uart->BDL = baud_rate & 255;
}

void
uart_tx_enable(struct uart_ctx *ctx, struct fifo_ctx *tx_fifo)
{
        crit_enter();
        ctx->tx_fifo = tx_fifo;
        ctx->uart->C2 |= 8; // enable transmitter, don't enable interrupts
        // have any data to send
        if (ctx->uart == UART0_BASE_PTR)
                pin_mode(PIN_PTA2, PIN_MODE_MUX_ALT2);
        if (ctx->uart == UART1_BASE_PTR)
                pin_mode(PIN_PTC4, PIN_MODE_MUX_ALT2);
        if (ctx->uart == UART2_BASE_PTR)
                pin_mode(PIN_PTD3, PIN_MODE_MUX_ALT2);
        // XXXKF UART1 and 2

        crit_exit();
}

void
uart_tx_set_invert(struct uart_ctx *ctx, int invert)
{
        if (invert)
                ctx->uart->C3 |= 16;
        else
                ctx->uart->C3 &= ~16;
}

void
uart_rx_enable(struct uart_ctx *ctx,
               uart_rx_cb rx_cb,
               void *rx_cbdata)
{
        crit_enter();
        ctx->rx_cb = rx_cb;
        ctx->rx_cbdata = rx_cbdata;
        int data = 0;
        ctx->uart->C2 |= 4 | 32;
        // XXXKF needs parameter for pin selection
        if (ctx->uart == UART0_BASE_PTR)
                pin_mode(PIN_PTA1, PIN_MODE_MUX_ALT2);
        if (ctx->uart == UART1_BASE_PTR)
                pin_mode(PIN_PTC3, PIN_MODE_MUX_ALT2);
        if (ctx->uart == UART2_BASE_PTR)
                pin_mode(PIN_PTD2, PIN_MODE_MUX_ALT2);
        // XXXKF UART1 and 2
        crit_exit();
}

void
uart_tx_dispatch(struct uart_ctx *ctx)
{
        if (!ctx->tx_fifo)
                return;
        crit_enter();
        // if there's space in UART TX FIFO, push more data
        if (fifo_get_read_len(ctx->tx_fifo))
                ctx->uart->D = fifo_read_byte(ctx->tx_fifo);

        // More data to push in the queue? send an interrupt when finished
        if (fifo_get_read_len(ctx->tx_fifo))
                ctx->uart->C2 |= 128;
        else
                ctx->uart->C2 &= ~128;
        crit_exit();
        
}

static void
uart_status_handler(struct uart_ctx *ctx)
{
        int status = ctx->uart->S1;
        if (status & 128) {
                uart_tx_dispatch(ctx);
        }
        if (status & 32) {
                int data = ctx->uart->D;
                if (ctx->rx_cb)
                        ctx->rx_cb(ctx->rx_cbdata, data);
        }
}

void
UART0_status_Handler(void)
{
        uart_status_handler(uart_contexts[0]);
}

void
UART1_status_Handler(void)
{
        uart_status_handler(uart_contexts[1]);
}

void
UART2_status_Handler(void)
{
        uart_status_handler(uart_contexts[2]);
}

void
uart_tx(struct uart_ctx *ctx,
        uint8_t byte)
{
        crit_enter();
        if ((ctx->uart->S1 & 128) && !fifo_get_read_len(ctx->tx_fifo))
                ctx->uart->D = byte;
        else
        {
                fifo_write_byte(ctx->tx_fifo, byte);
                ctx->uart->C2 |= 128;
        }
        crit_exit();
}

