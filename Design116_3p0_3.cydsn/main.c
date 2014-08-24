/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include <project.h>

// F-RAM Chip Attributes
#define     FM_ADDR_BITS        (18)
#define     FM_ADDR_MASK        ((0x00000001uL<<FM_ADDR_BITS)-1)

// The size of a data packet
#define     PACKET_SIZE         (64)

// DMA Configuration for DMA_TX
#define     DMA_TX_BYTES_PER_BURST      (1)
#define     DMA_TX_REQUEST_PER_BURST    (1)
#define     DMA_TX_SRC_BASE             (CYDEV_SRAM_BASE)
#define     DMA_TX_DST_BASE             (CYDEV_PERIPH_BASE)

// DMA Configuration for DMA_RX
#define     DMA_RX_BYTES_PER_BURST      (1)
#define     DMA_RX_REQUEST_PER_BURST    (1)
#define     DMA_RX_SRC_BASE             (CYDEV_PERIPH_BASE)
#define     DMA_RX_DST_BASE             (CYDEV_SRAM_BASE)

// Command op-code for F-RAM
enum FramCommands {
    FM_WRITE = 0x02,            // WRITE command
    FM_READ = 0x03,             // READ command
    FM_WREN = 0x06,             // WREN command
};

// F-RAM REad/WRITE buffer structure
union FramBuffer {
    uint8           stream[PACKET_SIZE + 4];
    struct {
        uint8       command;            // op-code
        uint8       addr[3];            // MSB to LSB
        uint8       d[PACKET_SIZE];     // a packet of data
    } s;
};

// Buffer for SPI's MOSI and MISO
union FramBuffer    mosiBuffer;
union FramBuffer    misoBuffer;

// Variable declarations for DMA_TX
// Move these variable declarations to the top of the function
uint8 DMA_TX_Chan;
uint8 DMA_TX_TD[1];

// Variable declarations for DMA_RX
// Move these variable declarations to the top of the function
uint8 DMA_RX_Chan;
uint8 DMA_RX_TD[1];
CYBIT DMA_RX_completed;

void framDmaInit(void) {
    // Initialize DMA TX descriptors
    DMA_TX_Chan = DMA_TX_DmaInitialize(
        DMA_TX_BYTES_PER_BURST, DMA_TX_REQUEST_PER_BURST, 
        HI16(DMA_TX_SRC_BASE), HI16(DMA_TX_DST_BASE)
    );
    DMA_TX_TD[0] = CyDmaTdAllocate();
    CyDmaTdSetConfiguration(DMA_TX_TD[0],
        sizeof mosiBuffer,
        CY_DMA_DISABLE_TD,
        TD_INC_SRC_ADR
    );
    CyDmaTdSetAddress(DMA_TX_TD[0],
        LO16((uint32)mosiBuffer.stream), LO16((uint32)SPIM_TXDATA_PTR)
    );
    CyDmaChSetInitialTd(DMA_TX_Chan, DMA_TX_TD[0]);
    
    // Initialize DMA RX descriptors
    DMA_RX_Chan = DMA_RX_DmaInitialize(
        DMA_RX_BYTES_PER_BURST, DMA_RX_REQUEST_PER_BURST, 
        HI16(DMA_RX_SRC_BASE), HI16(DMA_RX_DST_BASE)
    );
    DMA_RX_TD[0] = CyDmaTdAllocate();
    CyDmaTdSetConfiguration(DMA_RX_TD[0],
        sizeof misoBuffer,
        CY_DMA_DISABLE_TD,
        TD_INC_DST_ADR | DMA_RX__TD_TERMOUT_EN
    );
    CyDmaTdSetAddress(DMA_RX_TD[0],
        LO16((uint32)SPIM_RXDATA_PTR), LO16((uint32)misoBuffer.stream)
    );
    CyDmaChSetInitialTd(DMA_RX_Chan, DMA_RX_TD[0]);
}

// Select a F-RAM chip from the address
void framChipSelect(uint32 address) {
    static CYCODE uint8 ss_pattern[8] = {
        0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F,
    };
    uint8       chip = address >> FM_ADDR_BITS;
    
    SS_Write(ss_pattern[chip]);
}

// Deselect all F-RAMs
void framChipDeselect(void) {
    SS_Write(0xFF);
}

// Send a WREN command for an address
void framWriteEnable(uint32 address) {
    // assert SS
    framChipSelect(address);
    
    // Send command
    SPIM_WriteTxData(FM_WREN);
    
    // Wait for transfer completed
    while (!(SPIM_ReadRxStatus() & SPIM_STS_RX_FIFO_NOT_EMPTY)) ;
    
    // Negate SS
    framChipDeselect();

    // Drop MISO data
    SPIM_ClearRxBuffer();
}

// Send a WRITE command for an address
void framWritePacket(uint32 address) {
    // Send command and address
    mosiBuffer.s.command = FM_WRITE;
    mosiBuffer.s.addr[0] = address >> 16;
    mosiBuffer.s.addr[1] = address >>  8;
    mosiBuffer.s.addr[2] = address >>  0;

    // assert SS
    framChipSelect(address);
    
    // Enable DMA for RX
    DMA_RX_completed = 0;
    CyDmaClearPendingDrq(DMA_RX_Chan);
    CyDmaChEnable(DMA_RX_Chan, 1);

    // Enable DMA for TX
    CyDmaChEnable(DMA_TX_Chan, 1);
   
    // Trigger DMA for TX
    CyDmaChSetRequest(DMA_TX_Chan, CY_DMA_CPU_REQ);
        
    // Wait for transfer completed
    while (!DMA_RX_completed) ;

    // Negate SS
    framChipDeselect();
}

// Send a READ command for an address
void framReadPacket(uint32 address) {
    // assert SS
    framChipSelect(address);
    
    // Send command and address
    mosiBuffer.s.command = FM_READ;
    mosiBuffer.s.addr[0] = address >> 16;
    mosiBuffer.s.addr[1] = address >>  8;
    mosiBuffer.s.addr[2] = address >>  0;
        
    // Enable DMA for RX
    DMA_RX_completed = 0;
    CyDmaClearPendingDrq(DMA_RX_Chan);
    CyDmaChEnable(DMA_RX_Chan, 1);

    // Enable DMA for TX
    CyDmaChEnable(DMA_TX_Chan, 1);
    
    // Trigger DMA for TX
    CyDmaChSetRequest(DMA_TX_Chan, CY_DMA_CPU_REQ);
        
    // Wait for transfer completed
    while (!DMA_RX_completed) ;
    
    // Negate SS
    framChipDeselect();
}

CY_ISR(DMA_RX_INT_ISR) {
    DMA_RX_completed = 1;
}

int main()
{
    uint8       d = 16;
    uint8       i;
    uint32      address = 0;
    
    // Initialize LCD and SPIM
    LCD_Start();
    SPIM_Start();
    framDmaInit();
    DMA_RX_INT_StartEx(DMA_RX_INT_ISR);

    CyGlobalIntEnable; /* Uncomment this line to enable global interrupts. */
    
    for (;;) {
        for (;;) {
            // Debounce delay
            CyDelay(10);

            // Wait for button released
            while (!SW2_Read() || !SW3_Read()) ;
            
            // Debounce delay
            CyDelay(10);
            
            for (;;) {
                // READ operation
                if (!SW2_Read()) {
                    // Initialize MISO buffer
                    for (i = 0; i < sizeof misoBuffer; i++) {
                        misoBuffer.stream[i] = ~i;
                    }
                    
                    // Read a packet
                    framReadPacket(address);
                    
                    // Show the packet
                    LCD_ClearDisplay();
                    for (i = 0; i < 8; i++) {
                        LCD_PrintHexUint8(misoBuffer.s.d[i]);
                    }
                    LCD_Position(1, 0);
                    for (i = PACKET_SIZE-8; i < PACKET_SIZE; i++) {
                        LCD_PrintHexUint8(misoBuffer.s.d[i]);
                    }
                    break;
                }
                // WRITE operation
                if (!SW3_Read()) {
                    // Show address and data
                    LCD_ClearDisplay();
                    LCD_PrintHexUint16(HI16(address));
                    LCD_PrintHexUint16(LO16(address));
                    LCD_PrintString(" : ");
                    LCD_PrintHexUint8(d);
                    
                    // Set data to the buffer
                    for (i = 0; i < PACKET_SIZE; i+=2) {
                        mosiBuffer.s.d[i] = d;
                        mosiBuffer.s.d[i+1] = i;
                    }
                    
                    // Enable F-RAM WRITE
                    framWriteEnable(address);
                    
                    // Write a packet
                    framWritePacket(address);
                    
                    // Update data value
                    d++;
                    break;
                }
            }
            // Go to next packet
            address += PACKET_SIZE;
        }
    }
}

/* [] END OF FILE */
