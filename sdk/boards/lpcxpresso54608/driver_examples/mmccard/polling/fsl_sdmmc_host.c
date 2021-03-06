/*
 * The Clear BSD License
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
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

#include "fsl_sdmmc_host.h"
#include "fsl_sdmmc_event.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*!
 * @brief SDMMCHOST detect card insert status by host controller.
 * @param base host base address.
 * @param data3 flag indicate use data3 to detect card or not.
 */
static void SDMMCHOST_DetectCardInsertByHost(SDMMCHOST_TYPE *base, bool data3);

/*!
 * @brief SDMMCHOST detect card status by GPIO.
 */
static bool SDMMCHOST_DetectCardByGpio(void);

/*!
 * @brief SDMMCHOST transfer function.
 * @param base host base address.
 * @param content transfer configurations.
 */
static status_t SDMMCHOST_TransferFunction(SDMMCHOST_TYPE *base, SDMMCHOST_TRANSFER *content);

/*!
 * @brief card detect deinit function.
 */
static void SDMMCHOST_CardDetectDeinit(void);

/*!
 * @brief card detect deinit function.
 * @param host base address.
 * @param host detect card configuration.
 */
static status_t SDMMCHOST_CardDetectInit(SDMMCHOST_TYPE *base, const sdmmchost_detect_card_t *cd);

/*******************************************************************************
 * Variables
 ******************************************************************************/
/*! @brief Card detect flag. */
static volatile bool s_sdInsertedFlag = false;
/*! @brief DMA descriptor table. */
static uint32_t s_sdifDmaTable[SDIF_DMA_TABLE_WORDS];
/*******************************************************************************
 * Code
 ******************************************************************************/
static bool SDMMCHOST_DetectCardByGpio(void)
{
    if (SDMMCHOST_CARD_DETECT_STATUS() != SDMMCHOST_CARD_INSERT_CD_LEVEL)
    {
        s_sdInsertedFlag = false;
    }
    else
    {
        s_sdInsertedFlag = true;
    }

    return s_sdInsertedFlag;
}

static void SDMMCHOST_DetectCardInsertByHost(SDMMCHOST_TYPE *base, bool data3)
{
    if (SDMMCHOST_CARD_DETECT_INSERT_STATUS(base, data3))
    {
        s_sdInsertedFlag = true;
    }
}

/* User defined transfer function. */
static status_t SDMMCHOST_TransferFunction(SDMMCHOST_TYPE *base, SDMMCHOST_TRANSFER *content)
{
    status_t error = kStatus_Success;

    sdif_dma_config_t dmaConfig;

    memset(s_sdifDmaTable, 0, sizeof(s_sdifDmaTable));
    memset(&dmaConfig, 0, sizeof(dmaConfig));

    if (content->data != NULL)
    {
        dmaConfig.enableFixBurstLen = true;
        dmaConfig.mode = kSDIF_ChainDMAMode;
        dmaConfig.dmaDesBufferStartAddr = s_sdifDmaTable;
        dmaConfig.dmaDesBufferLen = SDIF_DMA_TABLE_WORDS;
    }

    if (kStatus_Success != SDIF_TransferBlocking(base, &dmaConfig, content))
    {
        error = kStatus_Fail;
    }

    return error;
}

static status_t SDMMCHOST_CardDetectInit(SDMMCHOST_TYPE *base, const sdmmchost_detect_card_t *cd)
{
    sdmmchost_detect_card_type_t cdType = kSDMMCHOST_DetectCardByGpioCD;

    if (cd != NULL)
    {
        cdType = cd->cdType;
    }

    if (cdType == kSDMMCHOST_DetectCardByGpioCD)
    {
        SDMMCHOST_CARD_DETECT_INIT();
        SDMMCHOST_DetectCardByGpio();
    }
    else
    {
        /* enable card detect status */
        SDMMCHOST_CARD_DETECT_INSERT_ENABLE(base);
    }

    return kStatus_Success;
}

static void SDMMCHOST_CardDetectDeinit(void)
{
    s_sdInsertedFlag = false;
}

void SDMMCHOST_Delay(uint32_t milliseconds)
{
    SDMMCEVENT_Delay(milliseconds);
}

status_t SDMMCHOST_WaitCardDetectStatus(SDMMCHOST_TYPE *base, const sdmmchost_detect_card_t *cd, bool waitCardStatus)
{
    sdmmchost_detect_card_type_t cdType = kSDMMCHOST_DetectCardByGpioCD;

    if (cd != NULL)
    {
        cdType = cd->cdType;
    }

    if (waitCardStatus != s_sdInsertedFlag)
    {
        /* Wait card inserted. */
        do
        {
            if (cdType == kSDMMCHOST_DetectCardByGpioCD)
            {
                SDMMCHOST_DetectCardByGpio();
            }
            else
            {
                SDMMCHOST_DetectCardInsertByHost(base, cdType == kSDMMCHOST_DetectCardByHostDATA3);
            }

        } while (waitCardStatus != s_sdInsertedFlag);
    }

    return kStatus_Success;
}

bool SDMMCHOST_IsCardPresent(void)
{
    return s_sdInsertedFlag;
}

void SDMMCHOST_PowerOffCard(SDMMCHOST_TYPE *base, const sdmmchost_pwr_card_t *pwr)
{
    if (pwr != NULL)
    {
        pwr->powerOff();
        SDMMCHOST_Delay(pwr->powerOffDelay_ms);
    }
    else
    {
        /* disable the card power */
        SDIF_EnableCardPower(base, false);
        /* Delay several milliseconds to make card stable. */
        SDMMCHOST_Delay(500U);
    }
}

void SDMMCHOST_PowerOnCard(SDMMCHOST_TYPE *base, const sdmmchost_pwr_card_t *pwr)
{
    /* use user define the power on function  */
    if (pwr != NULL)
    {
        pwr->powerOn();
        SDMMCHOST_Delay(pwr->powerOnDelay_ms);
    }
    else
    {
        /* Enable the card power */
        SDIF_EnableCardPower(base, true);
        /* Delay several milliseconds to make card stable. */
        SDMMCHOST_Delay(500U);
    }
}

status_t SDMMCHOST_Init(SDMMCHOST_CONFIG *host, void *userData)
{
    sdif_host_t *sdifHost = (sdif_host_t *)host;

    /* init event timer. */
    SDMMCEVENT_InitTimer();

    /* Initialize SDIF. */
    sdifHost->config.endianMode = kSDMMCHOST_EndianModeLittle;
    sdifHost->config.responseTimeout = 0xFFU;
    sdifHost->config.cardDetDebounce_Clock = 0xFFFFFFU;
    sdifHost->config.dataTimeout = 0xFFFFFFU;
    SDIF_Init(sdifHost->base, &(sdifHost->config));

    /* Define transfer function. */
    sdifHost->transfer = SDMMCHOST_TransferFunction;

    /* Enable the card power here for mmc card case, because mmc card don't need card detect*/
    SDIF_EnableCardPower(sdifHost->base, true);

    SDMMCHOST_CardDetectInit(sdifHost->base, (sdmmchost_detect_card_t *)userData);

    return kStatus_Success;
}

void SDMMCHOST_Reset(SDMMCHOST_TYPE *base)
{
    /* reserved for future */
}

void SDMMCHOST_Deinit(void *host)
{
    sdif_host_t *sdifHost = (sdif_host_t *)host;
    SDIF_Deinit(sdifHost->base);
    SDMMCHOST_CardDetectDeinit();
}
