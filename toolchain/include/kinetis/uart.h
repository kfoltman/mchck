#include <mchck.h>

struct UART_t {
        struct UART_BD_t {
                UNION_STRUCT_START(16);
                uint8_t sbrh:5;
                uint8_t _pad:1;
                uint8_t rxedgie:1;
                uint8_t lbkdie:1;
                uint8_t sbrl:8;
                UNION_STRUCT_END;
        } bd;
        struct UART_C1_t {
                UNION_STRUCT_START(8);
                uint8_t pt:1;
                uint8_t pe:1;
                uint8_t ilt:1;
                uint8_t wake:1;
                uint8_t m:1;
                uint8_t rsrc:1;
                uint8_t uartswai:1;
                uint8_t loops:1;
                UNION_STRUCT_END;
        } c1;
        struct UART_C2_t {
                UNION_STRUCT_START(8);
                uint8_t sbk:1;
                uint8_t rwu:1;
                uint8_t re:1;
                uint8_t te:1;
                uint8_t ilie:1;
                uint8_t rie:1;
                uint8_t tcie:1;
                uint8_t tie:1;
                UNION_STRUCT_END;
        } c2;
        struct UART_S1_t {
                UNION_STRUCT_START(8);
                uint8_t pf:1;
                uint8_t fe:1;
                uint8_t nf:1;
                uint8_t overrun:1;
                uint8_t idle:1;
                uint8_t rdrf:1;
                uint8_t tc:1;
                uint8_t tdre:1;
                UNION_STRUCT_END;
        } s1;
        struct UART_S2_t {
                UNION_STRUCT_START(8);
                uint8_t raf:1;
                uint8_t lbkde:1;
                uint8_t brk13:1;
                uint8_t rwuid:1;
                uint8_t rxinv:1;
                uint8_t msbf:1;
                uint8_t rxedgif:1;
                uint8_t lbkdif:1;
                UNION_STRUCT_END;
        } s2;
        struct UART_C3_t {
                UNION_STRUCT_START(8);
                uint8_t peie:1;
                uint8_t feie:1;
                uint8_t neie:1;
                uint8_t orie:1;
                uint8_t txinv:1;
                uint8_t txdir:1;
                uint8_t t8:1;
                uint8_t r8:1;
                UNION_STRUCT_END;
        } c3;
        uint8_t d;
        struct UART_MA_t {
                UNION_STRUCT_START(16);
                uint8_t ma1;
                uint8_t ma2;
                UNION_STRUCT_END;
        } ma;
        struct UART_C4_t {
                UNION_STRUCT_START(8);
                uint8_t brfa:5;
                uint8_t m10:1;
                uint8_t maen2:1;
                uint8_t maen1:1;
                UNION_STRUCT_END;
        } c4;
        struct UART_C5_t {
                UNION_STRUCT_START(8);
                uint8_t _pad0:5;
                uint8_t rdmas:1;
                uint8_t _pad1:1;
                uint8_t tdmas:1;
                UNION_STRUCT_END;
        } c5;
        struct UART_ED_t {
                UNION_STRUCT_START(8);
                uint8_t _pad0:6;
                uint8_t paritye:1;
                uint8_t noisy:1;
                UNION_STRUCT_END;
        } ed;
        struct UART_MODEM_t {
                UNION_STRUCT_START(8);
                uint8_t txctse:1;
                uint8_t txrtse:1;
                uint8_t txrtspol:1;
                uint8_t rxrtse:1;
                uint8_t _pad0:4;
                UNION_STRUCT_END;
        } modem;
        struct UART_IR_t {
                UNION_STRUCT_START(8);
                uint8_t tnp:2;
                uint8_t iren:1;
                uint8_t _pad0:5;
                UNION_STRUCT_END;
        } ir;
        uint8_t _pad0;
        /* FIFO registers */
        uint8_t PFIFO;
        uint8_t CFIFO;
        uint8_t SFIFO;
        uint8_t TWFIFO;
        uint8_t TCFIFO;
        uint8_t RWFIFO;
        uint8_t RCFIFO;
        uint8_t _pad1;
        /* ISO-7816 registers */
        uint8_t C7816;
        uint8_t IE7816;
        uint8_t IS7816;
        union {
                uint8_t WP7816T0;
                uint8_t WP7816T1;
        };
        uint8_t WN7816;
        uint8_t WF7816;
        uint8_t ET7816;
        uint8_t TL7816;
        /* CEA709.1-B registers */
        uint8_t _pad2;
        uint8_t C6;
        uint8_t PCTH;
        uint8_t PCTL;
        uint8_t B1T;
        uint8_t SDTH;
        uint8_t SDTL;
        uint8_t PRE;
        uint8_t TPL;
        uint8_t IE; /* Interrupt Enable */
        uint8_t WB; /* WBASE */
        uint8_t S3; /* Status 3 */
        uint8_t S4; /* Status 4 */
        uint8_t RPL; /* Received Packet Length */
        uint8_t RPREL; /* Received Preamble Length */
        uint8_t CPW; /* Collision Pulse Width */
        uint8_t RIDT; /* Receive Indeterminate Time */
        uint8_t TIDT; /* Transmit Indeterminate Time */
};

CTASSERT_SIZE_BYTE(struct UART_t, 50);

extern volatile struct UART_t UART0;
extern volatile struct UART_t UART1;
extern volatile struct UART_t UART2;
