Overview
========
This example shows how to use DMA to transfer data from DMIC to memory.

Toolchain supported
===================
- IAR embedded Workbench 8.11.3
- Keil MDK 5.23
- MCUXpresso10.1.0
- GCC ARM Embedded 6-2017-q2

Hardware requirements
=====================
- Micro USB cable
- LPCXpresso54608 board
- Personal Computer

Board settings
==============
No special settings are required.

Prepare the Demo
================
1.  Connect a micro USB cable between the host PC and the LPC-Link USB port (J8) on the target board.
2.  Open a serial terminal with the following settings:
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control
3.  Download the program to the target board.

Running the demo
================
1.  Either press the reset button on your board or launch the debugger in your IDE to begin running the demo.
Whole buffer before and after data transfer will be displayed. All data before transfer is 0 and data in the 
buffer after transfer is dmic data which is now different. 
The following lines are printed to the serial terminal when the demo program is executed. 

Configure DMA
Buffer Data before transfer
 <data>
Buffer Data after transfer
 <data>
Transfer completed


2. This example shows how DMA can be used with DMIC to transfer data to memory.DMIC audio data can also be seen in g_rxBuffer.
Intially this buffer is initialized to zero.
Customization options
=====================

