#include <mchck.h>

static struct uart_ctx *uart_contexts[3];

void
uart_init(struct uart_ctx *ctx,
          enum UART_CH_t port)
{
        switch(port) {
        case UART_CH0:
                SIM.scgc4.uart0 = 1;
                ctx->uart = &UART0;
                int_enable(IRQ_UART0_status);
                break;
        case UART_CH1:
                SIM.scgc4.uart1 = 1;
                ctx->uart = &UART1;
                int_enable(IRQ_UART1_status);
                break;
        case UART_CH2:
                SIM.scgc4.uart2 = 1;
                ctx->uart = &UART2;
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
        if (ctx->uart == &UART2)
                master_clock /= 1 + SIM.clkdiv1.outdiv2;
        baud_rate = ((master_clock << 2) / baud_rate + 1) >> 1;
        ctx->uart->c4.brfa = baud_rate & 31;
        baud_rate = baud_rate >> 5;
        ctx->uart->bd.sbrh = baud_rate >> 8;
        ctx->uart->bd.sbrl = baud_rate & 255;
}

void
uart_tx_enable(struct uart_ctx *ctx, struct fifo_ctx *tx_fifo)
{
        crit_enter();
        ctx->tx_fifo = tx_fifo;
        ctx->uart->c2.te = 1; // enable transmitter, don't enable interrupts
        // have any data to send
        if (ctx->uart == &UART0)
                pin_mode(PIN_PTA2, PIN_MODE_MUX_ALT2);
        if (ctx->uart == &UART1)
                pin_mode(PIN_PTC4, PIN_MODE_MUX_ALT2);
        if (ctx->uart == &UART2)
                pin_mode(PIN_PTD3, PIN_MODE_MUX_ALT2);
        // XXXKF UART1 and 2

        crit_exit();
}

void
uart_tx_set_invert(struct uart_ctx *ctx, int invert)
{
        ctx->uart->c3.txinv = invert ? 1 : 0;
}

void
uart_rx_enable(struct uart_ctx *ctx,
               uart_rx_cb rx_cb,
               void *rx_cbdata)
{
        crit_enter();
        ctx->rx_cb = rx_cb;
        ctx->rx_cbdata = rx_cbdata;
        ctx->uart->c2.re = 1;
        ctx->uart->c2.rie = 1;
        // XXXKF needs parameter for pin selection
        if (ctx->uart == &UART0)
                pin_mode(PIN_PTA1, PIN_MODE_MUX_ALT2);
        if (ctx->uart == &UART1)
                pin_mode(PIN_PTC3, PIN_MODE_MUX_ALT2);
        if (ctx->uart == &UART2)
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
                ctx->uart->d = fifo_read_byte(ctx->tx_fifo);

        // More data to push in the queue? send an interrupt when finished
        if (fifo_get_read_len(ctx->tx_fifo))
                ctx->uart->c2.tie = 1;
        else
                ctx->uart->c2.tie = 0;
        crit_exit();
        
}

static void
uart_status_handler(struct uart_ctx *ctx)
{
        struct UART_S1_t status = { .raw = ctx->uart->s1.raw };
        if (status.tdre) {
                uart_tx_dispatch(ctx);
        }
        if (status.rdrf) {
                int data = ctx->uart->d;
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
        if ((ctx->uart->s1.tdre) && !fifo_get_read_len(ctx->tx_fifo))
                ctx->uart->d = byte;
        else
        {
                fifo_write_byte(ctx->tx_fifo, byte);
                ctx->uart->c2.tie = 1;
        }
        crit_exit();
}

