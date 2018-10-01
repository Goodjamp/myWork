/*
 * The Clear BSD License
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 - 2017 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted (subject to the limitations in the disclaimer below) provided
 * that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_device_audio.h"

#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"

#include "audio_speaker.h"
#include "fsl_device_registers.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include <stdio.h>
#include <stdlib.h>
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */


#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)
#include "usb_phy.h"
#endif

#include "fsl_sctimer.h"

#include "pin_mux.h"
#include "fsl_i2c.h"
#include "fsl_i2s.h"
#include "fsl_i2s_dma.h"
#include "fsl_wm8904.h"
#include "fsl_power.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BOARD_I2S_DEMO_I2C_BASEADDR (I2C2)
#define DEMO_I2C_MASTER_CLOCK_FREQUENCY (12000000)
#define DEMO_I2S_TX (I2S0)
#define DEMO_DMA (DMA0)
#define DEMO_I2S_TX_CHANNEL (13)
#define I2S_CLOCK_DIVIDER (24576000 / 48000U / 16U / 2U)


#define USB_AUDIO_ENTER_CRITICAL() \
                                   \
    USB_OSA_SR_ALLOC();            \
                                   \
    USB_OSA_ENTER_CRITICAL()

#define USB_AUDIO_EXIT_CRITICAL() USB_OSA_EXIT_CRITICAL()
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void BOARD_InitHardware(void);
void USB_DeviceClockInit(void);
void USB_DeviceIsrEnable(void);
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle);
#endif

usb_status_t USB_DeviceAudioCallback(class_handle_t handle, uint32_t event, void *param);
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);

extern void BOARD_USB_Audio_TxInit(uint32_t samplingRate);
extern void BOARD_I2C_LPI2C_Init(void);
extern void BOARD_Codec_Init(void);

extern void BOARD_DMA_EDMA_Config(void);
extern void BOARD_Create_Audio_DMA_EDMA_Handle(void);
extern void BOARD_DMA_EDMA_Set_AudioFormat(void);
extern void BOARD_DMA_EDMA_Enable_Audio_Interrupts(void);
extern void BOARD_DMA_EDMA_Start(void);
extern void audio_trim_up(void);
extern void audio_trim_down(void);
/*******************************************************************************
 * Variables
 ******************************************************************************/
extern usb_audio_speaker_struct_t g_UsbDeviceAudioSpeaker;
extern uint8_t audioPlayDataBuff[AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE];
static i2s_config_t s_TxConfig;
static i2s_dma_handle_t s_TxHandle;
static dma_handle_t s_DmaTxHandle;
static i2s_transfer_t s_TxTransfer;

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t audioPlayDMATempBuff[FS_ISO_OUT_ENDP_PACKET_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t audioPlayDataBuff[AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t audioPlayPacket[FS_ISO_OUT_ENDP_PACKET_SIZE + AUDIO_FORMAT_CHANNELS * AUDIO_FORMAT_SIZE];

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
static uint32_t eventCounterL = 0;
static uint32_t captureRegisterNumber;
static sctimer_config_t sctimerInfo;
#endif

extern usb_device_class_struct_t g_UsbDeviceAudioClass;

/* Default value of audio generator device struct */
usb_audio_speaker_struct_t g_UsbDeviceAudioSpeaker = {
    .deviceHandle = NULL,
    .audioHandle = (class_handle_t)NULL,
    .currentStreamOutMaxPacketSize = (FS_ISO_OUT_ENDP_PACKET_SIZE),
    .speed = USB_SPEED_FULL,
    .attach = 0U,
    .copyProtect = 0x01U,
    .curMute = 0x00U,
    .curVolume = {0x00U, 0x80U},
    .minVolume = {0x00U, 0x80U},
    .maxVolume = {0xFFU, 0x7FU},
    .resVolume = {0x01U, 0x00U},
    .curBass = 0x00U,
    .minBass = 0x80U,
    .maxBass = 0x7FU,
    .resBass = 0x01U,
    .curMid = 0x00U,
    .minMid = 0x80U,
    .maxMid = 0x7FU,
    .resMid = 0x01U,
    .curTreble = 0x01U,
    .minTreble = 0x80U,
    .maxTreble = 0x7FU,
    .resTreble = 0x01U,
    .curAutomaticGain = 0x01U,
    .curDelay = {0x00U, 0x40U},
    .minDelay = {0x00U, 0x00U},
    .maxDelay = {0xFFU, 0xFFU},
    .resDelay = {0x00U, 0x01U},
    .curLoudness = 0x01U,
    .curSamplingFrequency = {0x00U, 0x00U, 0x01U},
    .minSamplingFrequency = {0x00U, 0x00U, 0x01U},
    .maxSamplingFrequency = {0x00U, 0x00U, 0x01U},
    .resSamplingFrequency = {0x00U, 0x00U, 0x01U},
#if USBCFG_AUDIO_CLASS_2_0
    .curSampleFrequency = 48000U, /* This should be changed to 48000 if sampling rate is 48k */
    .curClockValid = 1U,
    .controlRange = {1U, 48000U, 48000U, 0U},
#endif
    .speed = USB_SPEED_FULL,
    .tdReadNumberPlay = 0,
    .tdWriteNumberPlay = 0,
    .audioSendCount = 0,
    .lastAudioSendCount = 0,
    .usbRecvCount = 0,
    .audioSendTimes = 0,
    .usbRecvTimes = 0,
    .startPlay = 0,
    .startPlayHalfFull = 0,
    .speakerIntervalCount = 0,
    .speakerReservedSpace = 0,
    .timesFeedbackCalculate = 0,
    .speakerDetachOrNoInput = 0,
    .curAudioPllFrac = AUDIO_PLL_FRACTIONAL_DIVIDER,
    .audioPllTicksPrev = 0,
    .audioPllTicksDiff = 0,
    .audioPllTicksEma = AUDIO_PLL_USB1_SOF_INTERVAL_COUNT,
    .audioPllTickEmaFrac = 0,
    .audioPllStep = AUDIO_PLL_FRACTIONAL_CHANGE_STEP,
};

/* USB device class information */
static usb_device_class_config_struct_t s_audioConfig[1] = {{
    USB_DeviceAudioCallback, (class_handle_t)NULL, &g_UsbDeviceAudioClass,
}};

/* USB device class configuraion information */
static usb_device_class_config_list_struct_t s_audioConfigList = {
    s_audioConfig, USB_DeviceCallback, 1U,
};

/*******************************************************************************
* Code
******************************************************************************/
extern void WM8904_USB_Audio_Init(void *I2CBase);
void WM8904_Config_Audio_Formats(uint32_t samplingRate);

void audio_trim_up(void)
{
    uint32_t val = SYSCON->FROCTRL;
    val = (val & ~(0xff << 16)) | ((((val >> 16) & 0xFF) + 1) << 16) | (1UL << 31);
    SYSCON->FROCTRL = val;
}

void audio_trim_down(void)
{
    uint32_t val = SYSCON->FROCTRL;
    SYSCON->FROCTRL = (val & ~(0xff << 16)) | ((((val >> 16) & 0xFF) - 1) << 16) | (1UL << 31);
}


void BOARD_Codec_Init()
{
    WM8904_USB_Audio_Init(BOARD_I2S_DEMO_I2C_BASEADDR);
}

void I2S_USB_Audio_TxInit(I2S_Type *SAIBase)
{
    /*
     * masterSlave = kI2S_MasterSlaveNormalMaster;
     * mode = kI2S_ModeI2sClassic;
     * rightLow = false;
     * leftJust = false;
     * pdmData = false;
     * sckPol = false;
     * wsPol = false;
     * divider = 1;
     * oneChannel = false;
     * dataLength = 16;
     * frameLength = 32;
     * position = 0;
     * watermark = 4;
     * txEmptyZero = true;
     * pack48 = false;
     */
    I2S_TxGetDefaultConfig(&s_TxConfig);
    s_TxConfig.divider = I2S_CLOCK_DIVIDER;
    I2S_TxInit(DEMO_I2S_TX, &s_TxConfig);
}

void BOARD_USB_Audio_TxInit(uint32_t samplingRate)
{
    I2S_USB_Audio_TxInit(DEMO_I2S_TX);
}

void BOARD_I2C_LPI2C_Init()
{
    i2c_master_config_t i2cConfig;
    /*
     * enableMaster = true;
     * baudRate_Bps = 100000U;
     * enableTimeout = false;
     */
    I2C_MasterGetDefaultConfig(&i2cConfig);
    i2cConfig.baudRate_Bps = WM8904_I2C_BITRATE;
    I2C_MasterInit(BOARD_I2S_DEMO_I2C_BASEADDR, &i2cConfig, DEMO_I2C_MASTER_CLOCK_FREQUENCY);
}

static void TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    if ((g_UsbDeviceAudioSpeaker.audioSendTimes >= g_UsbDeviceAudioSpeaker.usbRecvTimes) &&
        (g_UsbDeviceAudioSpeaker.startPlayHalfFull == 1))
    {
        g_UsbDeviceAudioSpeaker.startPlayHalfFull = 0;
        g_UsbDeviceAudioSpeaker.speakerDetachOrNoInput = 1;
    }
    if (g_UsbDeviceAudioSpeaker.startPlayHalfFull)
    {
        s_TxTransfer.dataSize = FS_ISO_OUT_ENDP_PACKET_SIZE;
        s_TxTransfer.data = audioPlayDataBuff + g_UsbDeviceAudioSpeaker.tdWriteNumberPlay;
        g_UsbDeviceAudioSpeaker.audioSendCount += FS_ISO_OUT_ENDP_PACKET_SIZE;
        g_UsbDeviceAudioSpeaker.audioSendTimes++;
        g_UsbDeviceAudioSpeaker.tdWriteNumberPlay += FS_ISO_OUT_ENDP_PACKET_SIZE;
        if (g_UsbDeviceAudioSpeaker.tdWriteNumberPlay >=
            AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE)
        {
            g_UsbDeviceAudioSpeaker.tdWriteNumberPlay = 0;
        }
    }
    else
    {
        s_TxTransfer.dataSize = FS_ISO_OUT_ENDP_PACKET_SIZE;
        s_TxTransfer.data = audioPlayDMATempBuff;
    }
    I2S_TxTransferSendDMA(base, handle, s_TxTransfer);
}

void BOARD_DMA_EDMA_Config()
{
    DMA_Init(DEMO_DMA);

    DMA_EnableChannel(DEMO_DMA, DEMO_I2S_TX_CHANNEL);
    DMA_SetChannelPriority(DEMO_DMA, DEMO_I2S_TX_CHANNEL, kDMA_ChannelPriority3);
}

void BOARD_Create_Audio_DMA_EDMA_Handle()
{
    DMA_CreateHandle(&s_DmaTxHandle, DEMO_DMA, DEMO_I2S_TX_CHANNEL);
}

void BOARD_DMA_EDMA_Start()
{
    s_TxTransfer.dataSize = FS_ISO_OUT_ENDP_PACKET_SIZE;
    s_TxTransfer.data = audioPlayDMATempBuff;

    I2S_TxTransferCreateHandleDMA(DEMO_I2S_TX, &s_TxHandle, &s_DmaTxHandle, TxCallback, (void *)&s_TxTransfer);
    /* need to queue two transmit buffers so when the first one
     * finishes transfer, the other immediatelly starts */
    I2S_TxTransferSendDMA(DEMO_I2S_TX, &s_TxHandle, s_TxTransfer);
    I2S_TxTransferSendDMA(DEMO_I2S_TX, &s_TxHandle, s_TxTransfer);
}

#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
void USB0_IRQHandler(void)
{
    USB_DeviceLpcIp3511IsrFunction(g_UsbDeviceAudioSpeaker.deviceHandle);
}
#endif
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
void USB1_IRQHandler(void)
{
    USB_DeviceLpcIp3511IsrFunction(g_UsbDeviceAudioSpeaker.deviceHandle);
}
#endif
void USB_DeviceClockInit(void)
{
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    /* enable USB IP clock */
    CLOCK_EnableUsbfs0DeviceClock(kCLOCK_UsbSrcFro, CLOCK_GetFroHfFreq());
    SYSCON->FROCTRL = (SYSCON->FROCTRL & ~(SYSCON_FROCTRL_USBCLKADJ_MASK));
#if defined(FSL_FEATURE_USB_USB_RAM) && (FSL_FEATURE_USB_USB_RAM)
    for (int i = 0; i < FSL_FEATURE_USB_USB_RAM; i++)
    {
        ((uint8_t *)FSL_FEATURE_USB_USB_RAM_BASE_ADDRESS)[i] = 0x00U;
    }
#endif

#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)
    /* enable USB IP clock */
    CLOCK_EnableUsbhs0DeviceClock(kCLOCK_UsbSrcUsbPll, 0U);
#if defined(FSL_FEATURE_USBHSD_USB_RAM) && (FSL_FEATURE_USBHSD_USB_RAM)
    for (int i = 0; i < FSL_FEATURE_USBHSD_USB_RAM; i++)
    {
        ((uint8_t *)FSL_FEATURE_USBHSD_USB_RAM_BASE_ADDRESS)[i] = 0x00U;
    }
#endif
#endif
}
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    uint8_t usbDeviceIP3511Irq[] = USB_IRQS;
    irqNumber = usbDeviceIP3511Irq[CONTROLLER_ID - kUSB_ControllerLpcIp3511Fs0];
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)
    uint8_t usbDeviceIP3511Irq[] = USBHSD_IRQS;
    irqNumber = usbDeviceIP3511Irq[CONTROLLER_ID - kUSB_ControllerLpcIp3511Hs0];
#endif
/* Install isr, set priority, and enable IRQ. */
#if defined(__GIC_PRIO_BITS)
    GIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
#else
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
#endif
    EnableIRQ((IRQn_Type)irqNumber);
}
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle)
{
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    USB_DeviceLpcIp3511TaskFunction(deviceHandle);
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)
    USB_DeviceLpcIp3511TaskFunction(deviceHandle);
#endif
}
#endif

/* Initialize the structure information for sai. */
void Init_Board_Sai_Codec(void)
{
    usb_echo("Init Audio SAI and CODEC\r\n");

    BOARD_USB_Audio_TxInit(AUDIO_SAMPLING_RATE);
    BOARD_I2C_LPI2C_Init();
    BOARD_Codec_Init();
    BOARD_DMA_EDMA_Config();
    BOARD_Create_Audio_DMA_EDMA_Handle();
    BOARD_DMA_EDMA_Start();
}

/*!
 * @brief Audio class specific request function.
 *
 * This function handles the Audio class specific requests.
 *
 * @param handle		  The Audio class handle.
 * @param event 		  The Audio class event type.
 * @param param 		  The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceAudioRequest(class_handle_t handle, uint32_t event, void *param)
{
    usb_device_control_request_struct_t *request = (usb_device_control_request_struct_t *)param;
    usb_status_t error = kStatus_USB_Success;

    switch (event)
    {
        case USB_DEVICE_AUDIO_GET_CUR_MUTE_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curMute;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curMute);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_VOLUME_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.curVolume;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curVolume);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_BASS_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curBass;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curBass);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_MID_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curMid;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curMid);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_TREBLE_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curTreble;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curTreble);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_AUTOMATIC_GAIN_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curAutomaticGain;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curAutomaticGain);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_DELAY_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.curDelay;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curDelay);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_SAMPLING_FREQ_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.curSamplingFrequency;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curSamplingFrequency);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_VOLUME_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.minVolume;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minVolume);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_BASS_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.minBass;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minBass);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_MID_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.minMid;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minMid);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_TREBLE_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.minTreble;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minTreble);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_DELAY_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.minDelay;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minDelay);
            break;
        case USB_DEVICE_AUDIO_GET_MIN_SAMPLING_FREQ_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.minSamplingFrequency;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.minSamplingFrequency);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_VOLUME_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.maxVolume;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxVolume);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_BASS_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.maxBass;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxBass);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_MID_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.maxMid;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxMid);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_TREBLE_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.maxTreble;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxTreble);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_DELAY_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.maxDelay;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxDelay);
            break;
        case USB_DEVICE_AUDIO_GET_MAX_SAMPLING_FREQ_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.maxSamplingFrequency;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.maxSamplingFrequency);
            break;
        case USB_DEVICE_AUDIO_GET_RES_VOLUME_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.resVolume;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resVolume);
            break;
        case USB_DEVICE_AUDIO_GET_RES_BASS_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.resBass;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resBass);
            break;
        case USB_DEVICE_AUDIO_GET_RES_MID_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.resMid;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resMid);
            break;
        case USB_DEVICE_AUDIO_GET_RES_TREBLE_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.resTreble;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resTreble);
            break;
        case USB_DEVICE_AUDIO_GET_RES_DELAY_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.resDelay;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resDelay);
            break;
        case USB_DEVICE_AUDIO_GET_RES_SAMPLING_FREQ_CONTROL:
            request->buffer = g_UsbDeviceAudioSpeaker.resSamplingFrequency;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.resSamplingFrequency);
            break;
#if USBCFG_AUDIO_CLASS_2_0
        case USB_DEVICE_AUDIO_GET_CUR_SAM_FREQ_CONTROL:
            request->buffer = (uint8_t *)&g_UsbDeviceAudioSpeaker.curSampleFrequency;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curSampleFrequency);
            break;
        case USB_DEVICE_AUDIO_GET_RANGE_SAM_FREQ_CONTROL:
            request->buffer = (uint8_t *)&g_UsbDeviceAudioSpeaker.controlRange;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.controlRange);
            break;
        case USB_DEVICE_AUDIO_GET_CUR_CLOCK_VALID_CONTROL:
            request->buffer = &g_UsbDeviceAudioSpeaker.curClockValid;
            request->length = sizeof(g_UsbDeviceAudioSpeaker.curClockValid);
            break;
#endif

        case USB_DEVICE_AUDIO_SET_CUR_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.curVolume;
            }
            else
            {
                uint16_t volume = (uint16_t)((uint16_t)g_UsbDeviceAudioSpeaker.curVolume[1] << 8U);
                volume |= (uint8_t)(g_UsbDeviceAudioSpeaker.curVolume[0]);
                g_UsbDeviceAudioSpeaker.codecTask |= VOLUME_CHANGE_TASK;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_MUTE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.curMute;
            }
            else
            {
                if (g_UsbDeviceAudioSpeaker.curMute)
                {
                    g_UsbDeviceAudioSpeaker.codecTask |= MUTE_CODEC_TASK;
                }
                else
                {
                    g_UsbDeviceAudioSpeaker.codecTask |= UNMUTE_CODEC_TASK;
                }
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.curBass;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.curMid;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.curTreble;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_AUTOMATIC_GAIN_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.curAutomaticGain;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.curDelay;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.curSamplingFrequency;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.minVolume;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.minBass;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.minMid;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.minTreble;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.minDelay;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MIN_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.minSamplingFrequency;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.maxVolume;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.maxBass;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.maxMid;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.maxTreble;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.maxDelay;
            }
            break;
        case USB_DEVICE_AUDIO_SET_MAX_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.maxSamplingFrequency;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_VOLUME_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.resVolume;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_BASS_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.resBass;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_MID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.resMid;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_TREBLE_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_UsbDeviceAudioSpeaker.resTreble;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_DELAY_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.resDelay;
            }
            break;
        case USB_DEVICE_AUDIO_SET_RES_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = g_UsbDeviceAudioSpeaker.resSamplingFrequency;
            }
            break;
#if USBCFG_AUDIO_CLASS_2_0
        case USB_DEVICE_AUDIO_SET_CUR_SAM_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = (uint8_t *)&g_UsbDeviceAudioSpeaker.curSampleFrequency;
            }
            break;
        case USB_DEVICE_AUDIO_SET_CUR_CLOCK_VALID_CONTROL:
        {
            request->buffer = &g_UsbDeviceAudioSpeaker.curClockValid;
        }
        break;

#endif
        default:
            error = kStatus_USB_InvalidRequest;
            break;
    }
    return error;
}

/* The USB_AudioSpeakerBufferSpaceAvailable() function gets the reserved speaker ringbuffer size */
uint32_t USB_AudioSpeakerBufferSpaceAvailable(void)
{
    if (g_UsbDeviceAudioSpeaker.tdReadNumberPlay > g_UsbDeviceAudioSpeaker.tdWriteNumberPlay)
    {
        g_UsbDeviceAudioSpeaker.speakerReservedSpace =
            g_UsbDeviceAudioSpeaker.tdReadNumberPlay - g_UsbDeviceAudioSpeaker.tdWriteNumberPlay;
    }
    else
    {
        g_UsbDeviceAudioSpeaker.speakerReservedSpace =
            g_UsbDeviceAudioSpeaker.tdReadNumberPlay +
            AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE -
            g_UsbDeviceAudioSpeaker.tdWriteNumberPlay;
    }
    return g_UsbDeviceAudioSpeaker.speakerReservedSpace;
}

#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
void USB_AudioFSSync()
{
    uint32_t reservedSpace = 0;

    if (g_UsbDeviceAudioSpeaker.speakerIntervalCount != AUDIO_CALCULATE_Ff_INTERVAL)
    {
        g_UsbDeviceAudioSpeaker.speakerIntervalCount++;
        return;
    }
    g_UsbDeviceAudioSpeaker.speakerIntervalCount = 1;

    reservedSpace = USB_AudioSpeakerBufferSpaceAvailable();
    if (reservedSpace >= AUDIO_BUFFER_UPPER_LIMIT(AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE))
    {
        audio_trim_up();
    }
    else if (reservedSpace <
             AUDIO_BUFFER_LOWER_LIMIT(AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE))
    {
        audio_trim_down();
    }
    else
    {
    }
}
#endif

/* The USB_AudioSpeakerPutBuffer() function fills the audioRecDataBuff with audioPlayPacket in every callback*/
void USB_AudioSpeakerPutBuffer(uint8_t *buffer, uint32_t size)
{
    while (size)
    {
        audioPlayDataBuff[g_UsbDeviceAudioSpeaker.tdReadNumberPlay] = *buffer;
        g_UsbDeviceAudioSpeaker.tdReadNumberPlay++;
        buffer++;
        size--;

        if (g_UsbDeviceAudioSpeaker.tdReadNumberPlay >=
            AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE)
        {
            g_UsbDeviceAudioSpeaker.tdReadNumberPlay = 0;
        }
    }
}

/*!
 * @brief Audio class specific callback function.
 *
 * This function handles the Audio class specific requests.
 *
 * @param handle		  The Audio class handle.
 * @param event 		  The Audio class event type.
 * @param param 		  The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceAudioCallback(class_handle_t handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_Error;
    usb_device_endpoint_callback_message_struct_t *ep_cb_param;
    ep_cb_param = (usb_device_endpoint_callback_message_struct_t *)param;

    switch (event)
    {
        case kUSB_DeviceAudioEventStreamRecvResponse:
            if ((g_UsbDeviceAudioSpeaker.attach) && (ep_cb_param->length != (USB_UNINITIALIZED_VAL_32)))
            {
                if (g_UsbDeviceAudioSpeaker.startPlay == 0)
                {
                    g_UsbDeviceAudioSpeaker.startPlay = 1;
                }
                if ((g_UsbDeviceAudioSpeaker.tdReadNumberPlay >=
                     AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH * FS_ISO_OUT_ENDP_PACKET_SIZE / 2) &&
                    (g_UsbDeviceAudioSpeaker.startPlayHalfFull == 0))
                {
                    g_UsbDeviceAudioSpeaker.startPlayHalfFull = 1;
                }
                USB_AudioSpeakerPutBuffer(audioPlayPacket, ep_cb_param->length);
                g_UsbDeviceAudioSpeaker.usbRecvCount += ep_cb_param->length;
                g_UsbDeviceAudioSpeaker.usbRecvTimes++;
#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
                USB_AUDIO_ENTER_CRITICAL();
                USB_AudioFSSync();
                USB_AUDIO_EXIT_CRITICAL();
#endif
                error = USB_DeviceAudioRecv(handle, USB_AUDIO_SPEAKER_STREAM_ENDPOINT, &audioPlayPacket[0],
                                            g_UsbDeviceAudioSpeaker.currentStreamOutMaxPacketSize);
            }
            break;

        default:
            if (param && (event > 0xFF))
            {
                error = USB_DeviceAudioRequest(handle, event, param);
            }
            break;
    }

    return error;
}

/* The USB_DeviceAudioSpeakerStatusReset() function resets the audio speaker status to the initialized status */
void USB_DeviceAudioSpeakerStatusReset(void)
{
    g_UsbDeviceAudioSpeaker.startPlay = 0;
    g_UsbDeviceAudioSpeaker.startPlayHalfFull = 0;
    g_UsbDeviceAudioSpeaker.tdReadNumberPlay = 0;
    g_UsbDeviceAudioSpeaker.tdWriteNumberPlay = 0;
    g_UsbDeviceAudioSpeaker.audioSendCount = 0;
    g_UsbDeviceAudioSpeaker.usbRecvCount = 0;
    g_UsbDeviceAudioSpeaker.lastAudioSendCount = 0;
    g_UsbDeviceAudioSpeaker.audioSendTimes = 0;
    g_UsbDeviceAudioSpeaker.usbRecvTimes = 0;
    g_UsbDeviceAudioSpeaker.speakerIntervalCount = 0;
    g_UsbDeviceAudioSpeaker.speakerReservedSpace = 0;
    g_UsbDeviceAudioSpeaker.timesFeedbackCalculate = 0;
    g_UsbDeviceAudioSpeaker.speakerDetachOrNoInput = 0;
    g_UsbDeviceAudioSpeaker.curAudioPllFrac = AUDIO_PLL_FRACTIONAL_DIVIDER;
    g_UsbDeviceAudioSpeaker.audioPllTicksPrev = 0;
    g_UsbDeviceAudioSpeaker.audioPllTicksDiff = 0;
    g_UsbDeviceAudioSpeaker.audioPllTicksEma = AUDIO_PLL_USB1_SOF_INTERVAL_COUNT;
    g_UsbDeviceAudioSpeaker.audioPllTickEmaFrac = 0;
    g_UsbDeviceAudioSpeaker.audioPllStep = AUDIO_PLL_FRACTIONAL_CHANGE_STEP;
}

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle		  The USB device handle.
 * @param event 		  The USB device event type.
 * @param param 		  The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_Error;
    uint8_t *temp8 = (uint8_t *)param;
    uint16_t *temp16 = (uint16_t *)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            g_UsbDeviceAudioSpeaker.attach = 0U;
            error = kStatus_USB_Success;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(CONTROLLER_ID, &g_UsbDeviceAudioSpeaker.speed))
            {
                USB_DeviceSetSpeed(handle, g_UsbDeviceAudioSpeaker.speed);
            }
            if (USB_SPEED_HIGH == g_UsbDeviceAudioSpeaker.speed)
            {
                g_UsbDeviceAudioSpeaker.currentStreamOutMaxPacketSize = (HS_ISO_OUT_ENDP_PACKET_SIZE);
            }

#endif
        }
        break;
        case kUSB_DeviceEventSetConfiguration:
            if (USB_AUDIO_SPEAKER_CONFIGURE_INDEX == (*temp8))
            {
                g_UsbDeviceAudioSpeaker.attach = 1U;
                g_UsbDeviceAudioSpeaker.currentConfiguration = *temp8;
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (g_UsbDeviceAudioSpeaker.attach)
            {
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);

                if (g_UsbDeviceAudioSpeaker.currentInterfaceAlternateSetting[interface] != alternateSetting)
                {
                    g_UsbDeviceAudioSpeaker.currentInterfaceAlternateSetting[interface] = alternateSetting;
                    if (alternateSetting == 1U)
                    {
                        USB_DeviceAudioSpeakerStatusReset();
                        USB_DeviceAudioRecv(g_UsbDeviceAudioSpeaker.audioHandle, USB_AUDIO_SPEAKER_STREAM_ENDPOINT,
                                            &audioPlayDataBuff[0],
                                            g_UsbDeviceAudioSpeaker.currentStreamOutMaxPacketSize);
                    }
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            if (param)
            {
                *temp8 = g_UsbDeviceAudioSpeaker.currentConfiguration;
                error = kStatus_USB_Success;
            }
            break;
        case kUSB_DeviceEventGetInterface:
            if (param)
            {
                /* Get current alternate setting of the interface request */
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                if (interface < USB_AUDIO_SPEAKER_INTERFACE_COUNT)
                {
                    *temp16 = (*temp16 & 0xFF00U) | g_UsbDeviceAudioSpeaker.currentInterfaceAlternateSetting[interface];
                    error = kStatus_USB_Success;
                }
                else
                {
                    error = kStatus_USB_InvalidRequest;
                }
            }
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (param)
            {
                /* Get device descriptor request */
                error = USB_DeviceGetDeviceDescriptor(handle, (usb_device_get_device_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (param)
            {
                /* Get device configuration descriptor request */
                error = USB_DeviceGetConfigurationDescriptor(handle,
                                                             (usb_device_get_configuration_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetStringDescriptor:
            if (param)
            {
                /* Get device string descriptor request */
                error = USB_DeviceGetStringDescriptor(handle, (usb_device_get_string_descriptor_struct_t *)param);
            }
            break;
        default:
            break;
    }

    return error;
}

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
void SCTIMER_SOF_TOGGLE_HANDLER()
{
    uint32_t currentSctCap = 0, pllCountPeriod = 0, pll_change = 0;
    uint32_t reservedSpace = 0;
    static int32_t pllCount = 0, pllDiff = 0;
    static int32_t err, abs_err;
    if (SCTIMER_GetStatusFlags(SCT0) & (1 << eventCounterL))
    {
        /* Clear interrupt flag.*/
        SCTIMER_ClearStatusFlags(SCT0, (1 << eventCounterL));
    }

    if (g_UsbDeviceAudioSpeaker.speakerIntervalCount != 100)
    {
        g_UsbDeviceAudioSpeaker.speakerIntervalCount++;
        return;
    }
    g_UsbDeviceAudioSpeaker.speakerIntervalCount = 1;
    currentSctCap = SCT0->SCTCAP[0];
    pllCountPeriod = currentSctCap - g_UsbDeviceAudioSpeaker.audioPllTicksPrev;
    g_UsbDeviceAudioSpeaker.audioPllTicksPrev = currentSctCap;
    pllCount = pllCountPeriod;
    if (g_UsbDeviceAudioSpeaker.attach)
    {
        if (abs(pllCount - AUDIO_PLL_USB1_SOF_INTERVAL_COUNT) < (AUDIO_PLL_USB1_SOF_INTERVAL_COUNT >> 7))
        {
            pllDiff = pllCount - g_UsbDeviceAudioSpeaker.audioPllTicksEma;
            g_UsbDeviceAudioSpeaker.audioPllTickEmaFrac += (pllDiff % 8);
            g_UsbDeviceAudioSpeaker.audioPllTicksEma += (pllDiff / 8) + g_UsbDeviceAudioSpeaker.audioPllTickEmaFrac / 8;
            g_UsbDeviceAudioSpeaker.audioPllTickEmaFrac = (g_UsbDeviceAudioSpeaker.audioPllTickEmaFrac % 8);

            err = g_UsbDeviceAudioSpeaker.audioPllTicksEma - AUDIO_PLL_USB1_SOF_INTERVAL_COUNT;
            abs_err = abs(err);
            if (abs_err > g_UsbDeviceAudioSpeaker.audioPllStep)
            {
                if (err > 0)
                {
                    g_UsbDeviceAudioSpeaker.curAudioPllFrac -= abs_err / g_UsbDeviceAudioSpeaker.audioPllStep;
                }
                else
                {
                    g_UsbDeviceAudioSpeaker.curAudioPllFrac += abs_err / g_UsbDeviceAudioSpeaker.audioPllStep;
                }
                pll_change = 1;
            }
            if (g_UsbDeviceAudioSpeaker.startPlayHalfFull)
            {
                reservedSpace = USB_AudioSpeakerBufferSpaceAvailable();
                if (reservedSpace > ((AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH / 2 + 1) * HS_ISO_OUT_ENDP_PACKET_SIZE))
                {
                    g_UsbDeviceAudioSpeaker.curAudioPllFrac++;
                    pll_change = 1;
                }
                else if (reservedSpace <
                         ((AUDIO_SPEAKER_DATA_WHOLE_BUFFER_LENGTH / 2 - 1) * HS_ISO_OUT_ENDP_PACKET_SIZE))
                {
                    g_UsbDeviceAudioSpeaker.curAudioPllFrac--;
                    pll_change = 1;
                }
            }
            if (pll_change)
            {
                SYSCON->AUDPLLFRAC = g_UsbDeviceAudioSpeaker.curAudioPllFrac;
                SYSCON->AUDPLLFRAC = g_UsbDeviceAudioSpeaker.curAudioPllFrac | (1U << SYSCON_AUDPLLFRAC_REQ_SHIFT);
            }
        }
    }
}

void SCTIMER_CaptureInit(void)
{
    INPUTMUX->SCT0_INMUX[eventCounterL] = 0x10U; /* 0x10U for USB1 and 0x0FU for USB0. */
    SCTIMER_GetDefaultConfig(&sctimerInfo);

    /* Switch to 16-bit mode */
    sctimerInfo.clockMode = kSCTIMER_Input_ClockMode;
    sctimerInfo.clockSelect = kSCTIMER_Clock_On_Rise_Input_7;

    /* Initialize SCTimer module */
    SCTIMER_Init(SCT0, &sctimerInfo);

    if (SCTIMER_SetupCaptureAction(SCT0, kSCTIMER_Counter_L, &captureRegisterNumber, eventCounterL) == kStatus_Fail)
    {
        usb_echo("SCT Setup Capture failed!\r\n");
    }
    SCT0->EVENT[0].STATE = 0x1;
    SCT0->EVENT[0].CTRL = (0x01 << 10) | (0x2 << 12);

    /* Enable interrupt flag for event associated with out 4, we use the interrupt to update dutycycle */
    SCTIMER_EnableInterrupts(SCT0, (1 << eventCounterL));

    /* Receive notification when event is triggered */
    SCTIMER_SetCallback(SCT0, SCTIMER_SOF_TOGGLE_HANDLER, eventCounterL);

    /* Enable at the NVIC */
    EnableIRQ(SCT0_IRQn);

    /* Start the L counter */
    SCTIMER_StartTimer(SCT0, kSCTIMER_Counter_L);
}
#endif

/*!
 * @brief Application initialization function.
 *
 * This function initializes the application.
 *
 * @return None.
 */

void APPInit(void)
{
    USB_DeviceClockInit();
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */
    Init_Board_Sai_Codec();

    if (kStatus_USB_Success !=
        USB_DeviceClassInit(CONTROLLER_ID, &s_audioConfigList, &g_UsbDeviceAudioSpeaker.deviceHandle))
    {
        usb_echo("USB device failed\r\n");
        return;
    }
    else
    {
        usb_echo("USB device audio speaker demo\r\n");
        g_UsbDeviceAudioSpeaker.audioHandle = s_audioConfigList.config->classHandle;
    }

    /* Install isr, set priority, and enable IRQ. */
    USB_DeviceIsrEnable();

    USB_DeviceRun(g_UsbDeviceAudioSpeaker.deviceHandle);

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
    SCTIMER_CaptureInit();
#endif
}

void USB_AudioCodecTask(void)
{
    if (g_UsbDeviceAudioSpeaker.codecTask & MUTE_CODEC_TASK)
    {
        usb_echo("Set Cur Mute : %x\r\n", g_UsbDeviceAudioSpeaker.curMute);
        g_UsbDeviceAudioSpeaker.codecTask &= ~MUTE_CODEC_TASK;
    }
    if (g_UsbDeviceAudioSpeaker.codecTask & UNMUTE_CODEC_TASK)
    {
        usb_echo("Set Cur Mute : %x\r\n", g_UsbDeviceAudioSpeaker.curMute);
        g_UsbDeviceAudioSpeaker.codecTask &= ~UNMUTE_CODEC_TASK;
    }
    if (g_UsbDeviceAudioSpeaker.codecTask & VOLUME_CHANGE_TASK)
    {
        usb_echo("Set Cur Volume : %x\r\n",
                 (uint16_t)(g_UsbDeviceAudioSpeaker.curVolume[1] << 8U) | g_UsbDeviceAudioSpeaker.curVolume[0]);
        g_UsbDeviceAudioSpeaker.codecTask &= ~VOLUME_CHANGE_TASK;
    }
}

void USB_AudioSpeakerResetTask(void)
{
    if (g_UsbDeviceAudioSpeaker.speakerDetachOrNoInput)
    {
        USB_DeviceAudioSpeakerStatusReset();
    }
}
/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
#if defined(__CC_ARM) || defined(__GNUC__)
int main(void)
#else
void main(void)
#endif
{
#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
    pll_config_t audio_pll_config = {
        .desiredRate = 24576000U, .inputRate = 12000000U,
    };
    pll_setup_t audio_pll_setup;
#endif
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
    pll_setup_t audio_pll_setup = {
        .pllctrl = 0U, .pllndec = 514U, .pllpdec = 29U, .pllmdec = 0U, .audpllfrac = AUDIO_PLL_FRACTIONAL_DIVIDER,
    };
#endif
    CLOCK_EnableClock(kCLOCK_InputMux);
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    /* USART0 clock */
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);
    /* I2C clock */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM2);
#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
    CLOCK_SetupAudioPLLData(&audio_pll_config, &audio_pll_setup);
    audio_pll_setup.flags = PLL_SETUPFLAG_POWERUP | PLL_SETUPFLAG_WAITLOCK;
    CLOCK_SetupAudioPLLPrec(&audio_pll_setup, audio_pll_setup.flags);
#endif
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
    audio_pll_setup.flags = PLL_SETUPFLAG_POWERUP | PLL_SETUPFLAG_WAITLOCK;
    SYSCON->AUDPLLCLKSEL =
        SYSCON_AUDPLLCLKSEL_SEL(0x01U); /* Select the external Crystal CLK_IN instead of FRO_12M for HS*/
    CLOCK_SetupAudioPLLPrecFract(&audio_pll_setup, audio_pll_setup.flags);
    SYSCON->SCTCLKSEL = SYSCON_AUDPLLCLKSEL_SEL(0x03U); /* Select the AUDIO PLL OUT for SCTimer source under HS*/

    CLOCK_SetClkDiv(kCLOCK_DivSctClk, 1, false);
/*  Configure Clock OUT for measurement
    CLOCK_AttachClk(kAUDIO_PLL_to_CLKOUT);

    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 1, false);
*/
#endif

    /* I2S clocks */
    CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM6);
    CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM7);
    /* Attach PLL clock to MCLK for I2S, no divider */
    CLOCK_AttachClk(kAUDIO_PLL_to_MCLK);
    SYSCON->MCLKDIV = SYSCON_MCLKDIV_DIV(0U);
    SYSCON->MCLKIO = 1U;
    /* reset FLEXCOMM for I2C */
    RESET_PeripheralReset(kFC2_RST_SHIFT_RSTn);
    /* reset FLEXCOMM for I2S */
    RESET_PeripheralReset(kFC6_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kFC7_RST_SHIFT_RSTn);
    /* Enable interrupts for I2S */
    EnableIRQ(FLEXCOMM6_IRQn);
    EnableIRQ(FLEXCOMM7_IRQn);
    /* Initialize the rest */
    BOARD_InitPins();
    BOARD_BootClockFROHF96M();
    BOARD_InitDebugConsole();
#if (defined USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS)
    POWER_DisablePD(kPDRUNCFG_PD_USB1_PHY);
    /* enable usb1 host clock */
    CLOCK_EnableClock(kCLOCK_Usbh1);
    *((uint32_t *)(USBHSH_BASE + 0x50)) |= USBHSH_PORTMODE_DEV_ENABLE_MASK;
    /* enable usb1 host clock */
    CLOCK_DisableClock(kCLOCK_Usbh1);
#endif
#if (defined USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS)
    POWER_DisablePD(kPDRUNCFG_PD_USB0_PHY); /*< Turn on USB Phy */
    CLOCK_SetClkDiv(kCLOCK_DivUsb0Clk, 1, false);
    CLOCK_AttachClk(kFRO_HF_to_USB0_CLK);
    /* enable usb0 host clock */
    CLOCK_EnableClock(kCLOCK_Usbhsl0);
    *((uint32_t *)(USBFSH_BASE + 0x5C)) |= USBFSH_PORTMODE_DEV_ENABLE_MASK;
    /* disable usb0 host clock */
    CLOCK_DisableClock(kCLOCK_Usbhsl0);
#endif

    APPInit();

    while (1)
    {
        USB_AudioCodecTask();

        USB_AudioSpeakerResetTask();

#if USB_DEVICE_CONFIG_USE_TASK
        USB_DeviceTaskFn(g_UsbDeviceAudioSpeaker.deviceHandle);
#endif
    }
}