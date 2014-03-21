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

// The size of a data packet
#define     PACKET_SIZE         (64)

// Command op-code for F-RAM
enum FmCommands {
    FM_WRITE = 0x02,            // WRITE command
    FM_READ = 0x03,             // READ command
    FM_WREN = 0x06,             // WREN command
};

// Buffer for SPI's MOSI and MISO
uint8       mosiBuffer[PACKET_SIZE];
uint8       misoBuffer[PACKET_SIZE];

void framWriteEnable(void) {
    // assert SS
    SS_Write(0);
    
    // Send command
    SPIM_WriteTxData(FM_WREN);
    
    // Wait for transfer completed
    while (!(SPIM_ReadTxStatus() & SPIM_STS_SPI_IDLE)) ;
    
    // Negate SS
    SS_Write(1);

    // Drop MISO data
    SPIM_ClearRxBuffer();
}

void framWritePacket(uint32 address) {
    // assert SS
    SS_Write(0);
    
    // Send command and address
    SPIM_WriteTxData(FM_WRITE);
    SPIM_WriteTxData(address >> 16);
    SPIM_WriteTxData(address >>  8);
    SPIM_WriteTxData(address >>  0);
    
    // Send dummy packet
    SPIM_PutArray(mosiBuffer, PACKET_SIZE);
    
    // Wait for transfer completed
    while (!(SPIM_ReadTxStatus() & SPIM_STS_SPI_IDLE)) ;
    
    // Negate SS
    SS_Write(1);

    // Drop MISO data
    SPIM_ClearRxBuffer();
}

void framReadPacket(uint32 address) {
    uint8       i;
    
    // assert SS
    SS_Write(0);
    
    // Send command and address
    SPIM_WriteTxData(FM_READ);
    SPIM_WriteTxData(address >> 16);
    SPIM_WriteTxData(address >>  8);
    SPIM_WriteTxData(address >>  0);
    
    // Send dummy packet
    SPIM_PutArray(mosiBuffer, PACKET_SIZE);
    
    // Wait for transfer completed
    while (!(SPIM_ReadTxStatus() & SPIM_STS_SPI_IDLE)) ;
    
    // Negate SS
    SS_Write(1);

    // Drop command and address part
    (void)SPIM_ReadRxData();
    (void)SPIM_ReadRxData();
    (void)SPIM_ReadRxData();
    (void)SPIM_ReadRxData();

    // Get received packet
    for (i = 0; i < PACKET_SIZE; i++) {
        misoBuffer[i] = SPIM_ReadRxData();
    }          
}

int main()
{
    uint8       d = 129;
    uint8       i;
    uint32      address = 0;
    
    // Initialize LCD and SPIM
    LCD_Start();
    SPIM_Start();

    CyGlobalIntEnable; /* Uncomment this line to enable global interrupts. */
    
    for (;;) {
        for (;;) {
            // Wait for button released
            while (!SW2_Read() || !SW3_Read()) ;
            
            for (;;) {
                // READ operation
                if (!SW2_Read()) {
                    // Read a packet
                    framReadPacket(address);
                    
                    // Show the packet
                    LCD_ClearDisplay();
                    for (i = 0; i < 8; i++) {
                        LCD_PrintHexUint8(misoBuffer[i]);
                    }
                    LCD_Position(1, 0);
                    for (i = PACKET_SIZE-8; i < PACKET_SIZE; i++) {
                        LCD_PrintHexUint8(misoBuffer[i]);
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
                        mosiBuffer[i] = d;
                        mosiBuffer[i+1] = i;
                    }
                    
                    // Enable F-RAM WRITE
                    framWriteEnable();
                    
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
