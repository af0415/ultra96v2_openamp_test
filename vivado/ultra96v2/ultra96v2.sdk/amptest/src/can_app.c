/******************************************************************************
*
* Copyright (C) 2010 - 2015 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/
/****************************************************************************/
/**
*
* @file xcanps_intr_example.c
*
* Contains an example of how to use the XCanPs driver directly.
* This example shows the usage of the driver/device in interrupt mode.
*
* @note
* The Baud Rate Prescaler Register (BRPR) and Bit Timing Register (BTR)
* are setup such that CAN baud rate equals 40Kbps, assuming that the
* the CAN clock is 24MHz. The user needs to modify these values based on
* the desired baud rate and the CAN clock frequency. For more information
* see the CAN 2.0A, CAN 2.0B, ISO 11898-1 specifications.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date	Changes
* ----- -----  -------- -----------------------------------------------
* 1.00a xd/sv  01/12/10 First release
* 2.1 adk 		23/08/14 Fixed CR:798792 Peripheral test for CANPS IP in
*						 SDK claims a 40kbps baud rate but it's not.
* 3.1   mus    01/14/16 Added support for intc interrupt controller
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xparameters.h"
#include "xcanps.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "test.h"
#include "FreeRTOS.h"
#include "semphr.h"
/************************** Constant Definitions *****************************/

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are defined here such that a user can easily
 * change all the needed parameters in one place.
 */

#define CAN_DEVICE_ID		XPAR_XCANPS_0_DEVICE_ID
#define CAN_INTR_VEC_ID		XPAR_XCANPS_0_INTR

/* Maximum CAN frame length in word */
#define XCANPS_MAX_FRAME_SIZE_IN_WORDS (XCANPS_MAX_FRAME_SIZE / sizeof(u32))

#define FRAME_DATA_LENGTH	8 /* Frame Data field length */

/*
 * Message Id Constant.
 */
#define TEST_MESSAGE_ID		2000

/*
 * The Baud Rate Prescaler Register (BRPR) and Bit Timing Register (BTR)
 * are setup such that CAN baud rate equals 40Kbps, assuming that the
 * the CAN clock is 24MHz. The user needs to modify these values based on
 * the desired baud rate and the CAN clock frequency. For more information
 * see the CAN 2.0A, CAN 2.0B, ISO 11898-1 specifications.
 */

/**
 * @brief 500k bps
 * 
 */
#define TEST_BTR_SYNCJUMPWIDTH		0
#define TEST_BTR_SECOND_TIMESEGMENT	5
#define TEST_BTR_FIRST_TIMESEGMENT	0


#define TEST_BRPR_BAUD_PRESCALAR	0x18

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

static int SetupCan( XCanPs *CanInstPtr,u16 CanDeviceId);
static void Config(XCanPs *InstancePtr);
static void SendFrame(XCanPs *InstancePtr);
static void SendHandler(void *CallBackRef);
static void RecvHandler(void *CallBackRef);
static void ErrorHandler(void *CallBackRef, u32 ErrorMask);
static void EventHandler(void *CallBackRef, u32 Mask);
static int SetupIntr(XCanPs *CanInstancePtr,u16 CanIntrId,XScuGic *IntcInstancePtr);

/************************** Variable Definitions *****************************/

static XCanPs CanInstance;    /* Instance of the Can driver */

/*
 * Buffers to hold frames to send and receive. These are declared as global so
 * that they are not on the stack.
 * These buffers need to be 32-bit aligned
 */
u32 TxFrame[32];
u32 RxFrame[32];

/*
 * Shared variables used to test the callbacks.
 */
volatile static int LoopbackError;	/* Asynchronous error occurred */
volatile static int RecvDone;		/* Received a frame */
volatile static int SendDone;		/* Frame was sent successfully */

extern SemaphoreHandle_t  BinarySemaphore ;
/**
 * @brief can初始化
 * 
 * @return int 
 */

int CanInit()
{
	int Status;

	xil_printf("CAN Interrupt Example Test \r\n");

	/*
	 * Run the Can interrupt example.
	 */
	Status = SetupCan(&CanInstance,CAN_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		xil_printf("CAN SetupCan Failed\r\n");
		return XST_FAILURE;
	}
	Status = SetupIntr(&CanInstance,CAN_INTR_VEC_ID,&InterruptController);
	if (Status != XST_SUCCESS) {
		xil_printf("CAN SetupIntr Failed\r\n");
		return XST_FAILURE;
	}
	
	return XST_SUCCESS;
}

/**
 * @brief can发送函数
 * 
 * @return int 
 */
int CanSend()
{
	SendFrame(&CanInstance); /* Send a frame */
	//xil_printf("CanSend Successfully \r\n");
	return XST_SUCCESS;
}

/**
 * @brief 配置can控制器
 * 
 * @param CanInstPtr 
 * @param CanDeviceId 
 * @return int 
 */

static int SetupCan( XCanPs *CanInstPtr,u16 CanDeviceId)
{
	int Status;
	XCanPs_Config *ConfigPtr;

	/*
	 * Initialize the Can device.
	 */
	ConfigPtr = XCanPs_LookupConfig(CanDeviceId);
	if (ConfigPtr == NULL) {
		return XST_FAILURE;
	}
	XCanPs_CfgInitialize(CanInstPtr,
				ConfigPtr,
				ConfigPtr->BaseAddr);
	xil_printf("CAN XCanPs_CfgInitialize SUCCESS\r\n");
	/*
	 * Run self-test on the device, which verifies basic sanity of the
	 * device and the driver.
	 */
	Status = XCanPs_SelfTest(CanInstPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	xil_printf("CAN XCanPs_SelfTest SUCCESS \r\n");
	/*
	 * Configure CAN device.
	 */
	Config(CanInstPtr);
	xil_printf("CAN Config SUCCESS \r\n");
	/*
	 * Set interrupt handlers.
	 */
	XCanPs_SetHandler(CanInstPtr, XCANPS_HANDLER_SEND,
			(void *)SendHandler, (void *)CanInstPtr);
	XCanPs_SetHandler(CanInstPtr, XCANPS_HANDLER_RECV,
			(void *)RecvHandler, (void *)CanInstPtr);
	XCanPs_SetHandler(CanInstPtr, XCANPS_HANDLER_ERROR,
			(void *)ErrorHandler, (void *)CanInstPtr);
	XCanPs_SetHandler(CanInstPtr, XCANPS_HANDLER_EVENT,
			(void *)EventHandler, (void *)CanInstPtr);
	xil_printf("CAN XCanPs_SetHandler SUCCESS \r\n");
	/*
	 * Initialize the flags.
	 */
	SendDone = FALSE;
	RecvDone = FALSE;
	LoopbackError = FALSE;

	XCanPs_EnterMode(CanInstPtr, XCANPS_MODE_NORMAL);
	while(XCanPs_GetMode(CanInstPtr) != XCANPS_MODE_NORMAL);
	xil_printf("CAN XCanPs_EnterMode SUCCESS \r\n");


	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function configures CAN device. Baud Rate Prescaler Register (BRPR) and
* Bit Timing Register (BTR) are set in this function.
*
* @param	InstancePtr is a pointer to the driver instance.
*
* @return	None.
*
* @note		If the CAN device is not working correctly, this function may
*		enter an infinite loop and will never return to the caller.
*
******************************************************************************/
static void Config(XCanPs *InstancePtr)
{
	/*
	 * Enter Configuration Mode if the device is not currently in
	 * Configuration Mode.
	 */
	XCanPs_EnterMode(InstancePtr, XCANPS_MODE_CONFIG);
	while(XCanPs_GetMode(InstancePtr) != XCANPS_MODE_CONFIG);

	/*
	 * Setup Baud Rate Prescaler Register (BRPR) and
	 * Bit Timing Register (BTR).
	 */
	XCanPs_SetBaudRatePrescaler(InstancePtr, TEST_BRPR_BAUD_PRESCALAR);
	XCanPs_SetBitTiming(InstancePtr, TEST_BTR_SYNCJUMPWIDTH,
					TEST_BTR_SECOND_TIMESEGMENT,
					TEST_BTR_FIRST_TIMESEGMENT);

}

/*****************************************************************************/
/**
*
* Send a CAN frame.
*
* @param	InstancePtr is a pointer to the driver instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void SendFrame(XCanPs *InstancePtr)
{
	u8 *FramePtr;
	int Index;
	int Status;

	/*
	 * Create correct values for Identifier and Data Length Code Register.
	 */
	TxFrame[0] = (u32)XCanPs_CreateIdValue((u32)TEST_MESSAGE_ID, 0, 0, 0, 0);
	TxFrame[1] = (u32)XCanPs_CreateDlcValue((u32)FRAME_DATA_LENGTH);

	/*
	 * Now fill in the data field with known values so we can verify them
	 * on receive.
	 */
	FramePtr = (u8 *)(&TxFrame[2]);
	for (Index = 0; Index < FRAME_DATA_LENGTH; Index++) {
		*FramePtr++ = (u8)Index;
	}

	/*
	 * Now wait until the TX FIFO is not full and send the frame.
	 */
	while (XCanPs_IsTxFifoFull(InstancePtr) == TRUE);

	Status = XCanPs_Send(InstancePtr, TxFrame);
	if (Status != XST_SUCCESS) {
		/*
		 * The frame could not be sent successfully.
		 */
		LoopbackError = TRUE;
		SendDone = TRUE;
		RecvDone = TRUE;
	}
}


/*****************************************************************************/
/**
*
* Callback function (called from interrupt handler) to handle confirmation of
* transmit events when in interrupt mode.
*
* @param	CallBackRef is the callback reference passed from the interrupt
*		handler, which in our case is a pointer to the driver instance.
*
* @return	None.
*
* @note		This function is called by the driver within interrupt context.
*
******************************************************************************/
static void SendHandler(void *CallBackRef)
{
	/*
	 * The frame was sent successfully. Notify the task context.
	 */
	SendDone = TRUE;
}


/*****************************************************************************/
/**
*
* Callback function (called from interrupt handler) to handle frames received in
* interrupt mode.  This function is called once per frame received.
* The driver's receive function is called to read the frame from RX FIFO.
*
* @param	CallBackRef is the callback reference passed from the interrupt
*		handler, which in our case is a pointer to the device instance.
*
* @return	None.
*
* @note		This function is called by the driver within interrupt context.
*
******************************************************************************/
static void RecvHandler(void *CallBackRef)
{
	static BaseType_t xHigherPriorityTaskWoken;
	//static int i=0;
	XCanPs *CanPtr = (XCanPs *)CallBackRef;
	int Status;
	int Index;
	u8 *FramePtr;
	//xil_printf("CAN RecvHandler In\r\n");
	Status = XCanPs_Recv(CanPtr, RxFrame);
	if (Status != XST_SUCCESS) {
		LoopbackError = TRUE;
		RecvDone = TRUE;
		return;
	}

	/*
	 * Verify Identifier and Data Length Code.
	 */
	if (RxFrame[0] != (u32)XCanPs_CreateIdValue((u32)TEST_MESSAGE_ID, 0, 0, 0, 0)) {
		LoopbackError = TRUE;
		RecvDone = TRUE;
		return;
	}

	if ((RxFrame[1] & ~XCANPS_DLCR_TIMESTAMP_MASK) != TxFrame[1]) {
		LoopbackError = TRUE;
		RecvDone = TRUE;
		return;
	}

	/*
	 * Verify the Data field contents.
	 */
	FramePtr = (u8 *)(&RxFrame[2]);
	for (Index = 0; Index < FRAME_DATA_LENGTH; Index++) {
		if (*FramePtr++ != (u8)Index) {
			LoopbackError = TRUE;
			break;
		}
	}
	RecvDone = TRUE;
	//TccGetCount();
	xSemaphoreGiveFromISR( BinarySemaphore, &xHigherPriorityTaskWoken );
	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	//xil_printf("C %d\r\n", i++);
	
}


/*****************************************************************************/
/**
*
* Callback function (called from interrupt handler) to handle error interrupt.
* Error code read from Error Status register is passed into this function.
*
* @param	CallBackRef is the callback reference passed from the interrupt
*		handler, which in our case is a pointer to the driver instance.
* @param	ErrorMask is a bit mask indicating the cause of the error.
*		Its value equals 'OR'ing one or more XCANPS_ESR_* defined in
*		xcanps_hw.h.
*
* @return	None.
*
* @note		This function is called by the driver within interrupt context.
*
******************************************************************************/
static void ErrorHandler(void *CallBackRef, u32 ErrorMask)
{

	if(ErrorMask & XCANPS_ESR_ACKER_MASK) {
		/*
		 * ACK Error handling code should be put here.
		 */
	}

	if(ErrorMask & XCANPS_ESR_BERR_MASK) {
		/*
		 * Bit Error handling code should be put here.
		 */
	}

	if(ErrorMask & XCANPS_ESR_STER_MASK) {
		/*
		 * Stuff Error handling code should be put here.
		 */
	}

	if(ErrorMask & XCANPS_ESR_FMER_MASK) {
		/*
		 * Form Error handling code should be put here.
		 */
	}

	if(ErrorMask & XCANPS_ESR_CRCER_MASK) {
		/*
		 * CRC Error handling code should be put here.
		 */
	}

	/*
	 * Set the shared variables.
	 */
	LoopbackError = TRUE;
	RecvDone = TRUE;
	SendDone = TRUE;
}


/*****************************************************************************/
/**
*
* Callback function (called from interrupt handler) to handle the following
* interrupts:
*   - XCANPS_IXR_BSOFF_MASK:	Bus Off Interrupt
*   - XCANPS_IXR_RXOFLW_MASK:	RX FIFO Overflow Interrupt
*   - XCANPS_IXR_RXUFLW_MASK:	RX FIFO Underflow Interrupt
*   - XCANPS_IXR_TXBFLL_MASK:	TX High Priority Buffer Full Interrupt
*   - XCANPS_IXR_TXFLL_MASK:	TX FIFO Full Interrupt
*   - XCANPS_IXR_WKUP_MASK:	Wake up Interrupt
*   - XCANPS_IXR_SLP_MASK:	Sleep Interrupt
*   - XCANPS_IXR_ARBLST_MASK:	Arbitration Lost Interrupt
*
*
* @param	CallBackRef is the callback reference passed from the
*		interrupt Handler, which in our case is a pointer to the
*		driver instance.
* @param	IntrMask is a bit mask indicating pending interrupts.
*		Its value equals 'OR'ing one or more of the XCANPS_IXR_*_MASK
*		value(s) mentioned above.
*
* @return	None.
*
* @note		This function is called by the driver within interrupt context.
* 		This function should be changed to meet specific application
*		needs.
*
******************************************************************************/
static void EventHandler(void *CallBackRef, u32 IntrMask)
{
	XCanPs *CanPtr = (XCanPs *)CallBackRef;

	if (IntrMask & XCANPS_IXR_BSOFF_MASK) {
		/*
		 * Entering Bus off status interrupt requires
		 * the CAN device be reset and reconfigured.
		 */
		XCanPs_Reset(CanPtr);
		Config(CanPtr);
		return;
	}

	if(IntrMask & XCANPS_IXR_RXOFLW_MASK) {
		/*
		 * Code to handle RX FIFO Overflow Interrupt should be put here.
		 */
	}

	if(IntrMask & XCANPS_IXR_RXUFLW_MASK) {
		/*
		 * Code to handle RX FIFO Underflow Interrupt
		 * should be put here.
		 */
	}

	if(IntrMask & XCANPS_IXR_TXBFLL_MASK) {
		/*
		 * Code to handle TX High Priority Buffer Full
		 * Interrupt should be put here.
		 */
	}

	if(IntrMask & XCANPS_IXR_TXFLL_MASK) {
		/*
		 * Code to handle TX FIFO Full Interrupt should be put here.
		 */
	}

	if (IntrMask & XCANPS_IXR_WKUP_MASK) {
		/*
		 * Code to handle Wake up from sleep mode Interrupt
		 * should be put here.
		 */
	}

	if (IntrMask & XCANPS_IXR_SLP_MASK) {
		/*
		 * Code to handle Enter sleep mode Interrupt should be put here.
		 */
	}

	if (IntrMask & XCANPS_IXR_ARBLST_MASK) {
		/*
		 * Code to handle Lost bus arbitration Interrupt
		 * should be put here.
		 */
	}
}


/**
 * @brief 
 * 
 * @param IntcInstancePtr 
 * @param CanInstancePtr 
 * @param CanIntrId 
 * @return int 
 */
static int SetupIntr(XCanPs *CanInstancePtr,
				u16 CanIntrId,XScuGic *IntcInstancePtr)
{
	int Status;

	/*
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, CanIntrId,
				(Xil_InterruptHandler)XCanPs_IntrHandler,
				(void *)CanInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * Enable the interrupt for the CAN device.
	 */
	XScuGic_Enable(IntcInstancePtr, CanIntrId);

	XCanPs_IntrEnable(CanInstancePtr, XCANPS_IXR_ALL);

	return XST_SUCCESS;
}