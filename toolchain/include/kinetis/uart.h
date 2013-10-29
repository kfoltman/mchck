#include <mchck.h>

__packed struct UART_t {
        struct UART_BD_t {
                UNION_STRUCT_START(16);
                unsigned sbrh:5;
                unsigned _pad:1;
                unsigned rxedgie:1;
                unsigned lbkdie:1;
                unsigned sbrl:8;
                UNION_STRUCT_END;
        } bd;
        struct UART_C1_t {
                UNION_STRUCT_START(8);
                unsigned pt:1;
                unsigned pe:1;
                unsigned ilt:1;
                unsigned wake:1;
                unsigned m:1;
                unsigned rsrc:1;
                unsigned uartswai:1;
                unsigned loops:1;
                UNION_STRUCT_END;
        } c1;
        struct UART_C2_t {
                UNION_STRUCT_START(8);
                unsigned sbk:1;
                unsigned rwu:1;
                unsigned re:1;
                unsigned te:1;
                unsigned ilie:1;
                unsigned rie:1;
                unsigned tcie:1;
                unsigned tie:1;
                UNION_STRUCT_END;
        } c2;
        struct UART_S1_t {
                UNION_STRUCT_START(8);
                unsigned pf:1;
                unsigned fe:1;
                unsigned nf:1;
                unsigned overrun:1;
                unsigned idle:1;
                unsigned rdrf:1;
                unsigned tc:1;
                unsigned tdre:1;
                UNION_STRUCT_END;
        } s1;
        struct UART_S2_t {
                UNION_STRUCT_START(8);
                unsigned raf:1;
                unsigned lbkde:1;
                unsigned brk13:1;
                unsigned rwuid:1;
                unsigned rxinv:1;
                unsigned msbf:1;
                unsigned rxedgif:1;
                unsigned lbkdif:1;
                UNION_STRUCT_END;
        } s2;
        struct UART_C3_t {
                UNION_STRUCT_START(8);
                unsigned peie:1;
                unsigned feie:1;
                unsigned neie:1;
                unsigned orie:1;
                unsigned txinv:1;
                unsigned txdir:1;
                unsigned t8:1;
                unsigned r8:1;
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
                unsigned brfa:5;
                unsigned m10:1;
                unsigned maen2:1;
                unsigned maen1:1;
                UNION_STRUCT_END;
        } c4;
        struct UART_C5_t {
                UNION_STRUCT_START(8);
                unsigned _pad0:5;
                unsigned rdmas:1;
                unsigned _pad1:1;
                unsigned tdmas:1;
                UNION_STRUCT_END;
        } c5;
        struct UART_ED_t {
                UNION_STRUCT_START(8);
                unsigned _pad0:6;
                unsigned paritye:1;
                unsigned noisy:1;
                UNION_STRUCT_END;
        } ed;
        struct UART_MODEM_t {
                UNION_STRUCT_START(8);
                unsigned txctse:1;
                unsigned txrtse:1;
                unsigned txrtspol:1;
                unsigned rxrtse:1;
                unsigned _pad0:4;
                UNION_STRUCT_END;
        } modem;
        struct UART_IR_t {
                UNION_STRUCT_START(8);
                unsigned tnp:2;
                unsigned iren:1;
                unsigned _pad0:5;
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
