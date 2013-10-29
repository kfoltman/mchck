enum UART_CH_t {
        UART_CH0 = 0,
        UART_CH1 = 1,
        UART_CH2 = 2
};

typedef void (*uart_rx_cb)(void *cbdata, uint8_t value);
typedef void (*uart_tx_empty_cb)(void *cbdata, uint8_t value);

struct uart_ctx {
        volatile struct UART_t *uart;
        struct fifo_ctx *tx_fifo;
        uart_rx_cb rx_cb;
        void *rx_cbdata;
        int tx_pending;
};

void
uart_init(struct uart_ctx *ctx,
          enum UART_CH_t port);

void
uart_set_baud_rate(struct uart_ctx *ctx, int baud_rate);

void
uart_tx_enable(struct uart_ctx *ctx, struct fifo_ctx *tx_fifo);

void
uart_rx_enable(struct uart_ctx *ctx,
               uart_rx_cb rx_cb,
               void *rx_cbdata);

void
uart_tx_set_invert(struct uart_ctx *ctx, int invert);

void
uart_tx(struct uart_ctx *ctx,
        uint8_t byte);

