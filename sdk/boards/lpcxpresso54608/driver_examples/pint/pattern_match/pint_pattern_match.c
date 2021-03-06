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

#include "fsl_debug_console.h"
#include "board.h"

#include "fsl_inputmux.h"
#include "fsl_pint.h"

#include "fsl_common.h"
#include "pin_mux.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEMO_PINT_PIN_INT0_SRC kINPUTMUX_GpioPort0Pin6ToPintsel
#define DEMO_PINT_PIN_INT1_SRC kINPUTMUX_GpioPort0Pin5ToPintsel
#define DEMO_PINT_PIN_INT2_SRC kINPUTMUX_GpioPort0Pin4ToPintsel

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
* @brief Call back for PINT Pin interrupt 0-7.
*/
void pint_intr_callback(pint_pin_int_t pintr, uint32_t pmatch_status)
{
    PRINTF("\f\r\nPINT Pin Interrupt %d event detected. PatternMatch status = %8b\r\n", pintr, pmatch_status);
}

/*!
* @brief Main function
*/
int main(void)
{
    pint_pmatch_cfg_t pmcfg;

    /* Board pin, clock, debug console init */
    /* attach 12 MHz clock to FLEXCOMM0 (debug console) */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM0);

    BOARD_InitPins();
    BOARD_BootClockFROHF48M();
    BOARD_InitDebugConsole();

    /* Clear screen*/
    PRINTF("%c[2J", 27);
    /* Set cursor location at [0,0] */
    PRINTF("%c[0;0H", 27);
    PRINTF("\f\r\nPINT Pattern Match example\r\n");

    /* Connect trigger sources to PINT */
    INPUTMUX_Init(INPUTMUX);
    INPUTMUX_AttachSignal(INPUTMUX, kPINT_PinInt0, DEMO_PINT_PIN_INT0_SRC);
    INPUTMUX_AttachSignal(INPUTMUX, kPINT_PinInt1, DEMO_PINT_PIN_INT1_SRC);
    INPUTMUX_AttachSignal(INPUTMUX, kPINT_PinInt2, DEMO_PINT_PIN_INT2_SRC);

    /* Turnoff clock to inputmux to save power. Clock is only needed to make changes */
    INPUTMUX_Deinit(INPUTMUX);

    /* Initialize PINT */
    PINT_Init(PINT);

    /* Setup Pattern Match Bit Slice 0 */
    pmcfg.bs_src = kPINT_PatternMatchInp0Src;
    pmcfg.bs_cfg = kPINT_PatternMatchStickyFall;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = true;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice0, &pmcfg);

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 1U)
    /* Setup Pattern Match Bit Slice 1 */
    pmcfg.bs_src = kPINT_PatternMatchInp0Src;
    pmcfg.bs_cfg = kPINT_PatternMatchStickyRise;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = false;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice1, &pmcfg);
#endif

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 2U)
    /* Setup Pattern Match Bit Slice 2 for falling edge detection */
    pmcfg.bs_src = kPINT_PatternMatchInp1Src;
    pmcfg.bs_cfg = kPINT_PatternMatchStickyRise;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = true;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice2, &pmcfg);
#endif

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 3U)
    /* Setup Pattern Match Bit Slice 3 for rising edge detection */
    pmcfg.bs_src = kPINT_PatternMatchInp2Src;
    pmcfg.bs_cfg = kPINT_PatternMatchStickyRise;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = true;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice3, &pmcfg);
#endif

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 4U)
    /* Setup Pattern Match Bit Slice 4  for rising edge detection */
    pmcfg.bs_src = kPINT_PatternMatchInp0Src;
    pmcfg.bs_cfg = kPINT_PatternMatchStickyRise;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = false;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice4, &pmcfg);
#endif

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 5U)
    /* Setup Pattern Match Bit Slice 5 for low level detection */
    pmcfg.bs_src = kPINT_PatternMatchInp1Src;
    pmcfg.bs_cfg = kPINT_PatternMatchLow;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = false;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice5, &pmcfg);
#endif

#if (FSL_FEATURE_PINT_NUMBER_OF_CONNECTED_OUTPUTS > 6U)
    /* Setup Pattern Match Bit Slice 6 for low level detection */
    pmcfg.bs_src = kPINT_PatternMatchInp2Src;
    pmcfg.bs_cfg = kPINT_PatternMatchLow;
    pmcfg.callback = pint_intr_callback;
    pmcfg.end_point = true;
    PINT_PatternMatchConfig(PINT, kPINT_PatternMatchBSlice6, &pmcfg);
#endif

    /* Enable PatternMatch */
    PINT_PatternMatchEnable(PINT);

    /* Enable callbacks for PINT */
    PINT_EnableCallback(PINT);

    PRINTF("\r\nPINT Pattern match events are configured\r\n");
    PRINTF("\r\nPress corresponding switches to generate events\r\n");
    while (1)
    {
        __WFI();
    }
}
