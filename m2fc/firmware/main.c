/*
 * M2FC
 * 2014 Adam Greig, Cambridge University Spaceflight
 */

#include <stdio.h>
#include <string.h>

#include "ch.h"
#include "hal.h"

#include "ff.h"

#include "ms5611.h"
#include "adxl3x5.h"
#include "config.h"
#include "pyro.h"
#include "datalogging.h"
#include "m2fc_shell.h"
#include "mission.h"
#include "state_estimation.h"
#include "analogue.h"
#include "l3g4200d.h"
#include "hmc5883l.h"
#include "dma_mutexes.h"
#include "m2serial.h"
#include "m2status.h"

/* Create working areas for all threads */
/* TODO: Move some stacks to CCM where possible (no DMA use). */
static WORKING_AREA(waMS5611, 768);
static WORKING_AREA(waADXL345, 512);
static WORKING_AREA(waADXL375, 512);
static WORKING_AREA(waMission, 1024);
static WORKING_AREA(waThreadHB, 128);
static WORKING_AREA(waDatalogging, 2048);
static WORKING_AREA(waConfig, 8192);
static WORKING_AREA(waPyros, 128);
/*static WORKING_AREA(waThreadSBP, 1024);*/
static WORKING_AREA(waAnalogue, 512);
static WORKING_AREA(waHMC5883L, 512);
static WORKING_AREA(waL3G4200D, 1024);
static WORKING_AREA(waM2Serial, 1024);
static WORKING_AREA(waM2Status, 1024);

/*
 * Heatbeat thread.
 * This thread flashes the everything-is-OK LED once a second,
 * and keeps resetting the watchdog timer for us.
 */
static msg_t ThreadHeartbeat(void *arg) {
    uint8_t mystatus;
    (void)arg;
    chRegSetThreadName("Heartbeat");

    while(TRUE) {
        /* Set the STATUS onboard LED */
        palSetPad(GPIOA, GPIOA_LED_STATUS);
        /* Set external LED */
        if(LocalStatus == &M2FCBodyStatus) {
            mystatus = M2FCBodyStatus.m2fcbody;
        } else if(LocalStatus == &M2FCNoseStatus) {
            mystatus = M2FCNoseStatus.m2fcnose;
        }
        if(mystatus == STATUS_OK) {
            /* Set the GREEN external LED */
            palClearPad(GPIOC, GPIOC_LED_C);
            palSetPad(GPIOC, GPIOC_LED_A);
        } else {
            /* Set the RED external LED */
            palSetPad(GPIOC, GPIOC_LED_C);
            palClearPad(GPIOC, GPIOC_LED_A);
        }
        /* Flash them briefly */
        chThdSleepMilliseconds(20);

        /* Turn LEDs off */
        palClearPad(GPIOA, GPIOA_LED_STATUS);
        palClearPad(GPIOC, GPIOC_LED_C);
        palClearPad(GPIOC, GPIOC_LED_A);

        /* Clear watchdog timer */
        IWDG->KR = 0xAAAA;
        chThdSleepMilliseconds(480);
    }

    return RDY_OK;
}

/*
 * Set up pin change interrupts for the various sensors that react to them.
 */
static const EXTConfig extcfg = {{
    {EXT_CH_MODE_AUTOSTART | EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOE,
        hmc5883l_wakeup},         /* Pin 0 - PE0 is the magnetometer DRDY */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 1 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 2 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 3 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 4 */
    {EXT_CH_MODE_AUTOSTART | EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOC,
        adxl375_wakeup},          /* Pin 5 - PC5 is the HG accel INT1 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 6 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 7 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 8 */
    {EXT_CH_MODE_AUTOSTART | EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOD,
        adxl345_wakeup},          /* Pin 9 - PD9 is the LG accel INT1 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 10 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 11 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 12 */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 13 */
    {EXT_CH_MODE_AUTOSTART | EXT_CH_MODE_RISING_EDGE | EXT_MODE_GPIOE,
        l3g4200d_wakeup},         /* Pin 14 - PE14 is the gyro DRDY */
    {EXT_CH_MODE_DISABLED, NULL}, /* Pin 15 */
    {EXT_CH_MODE_DISABLED, NULL}, /* 16 - PVD */
    {EXT_CH_MODE_DISABLED, NULL}, /* 17 - RTC Alarm */
    {EXT_CH_MODE_DISABLED, NULL}, /* 18 - USB OTG FS Wakeup */
    {EXT_CH_MODE_DISABLED, NULL}, /* 19 - Ethernet Wakeup */
    {EXT_CH_MODE_DISABLED, NULL}, /* 20 - USB OTG HS Wakeup */
    {EXT_CH_MODE_DISABLED, NULL}, /* 21 - RTC Tamper/Timestamp */
    {EXT_CH_MODE_DISABLED, NULL}  /* 22 - RTC Wakeup */
}};

/*
 * M2FC Main Thread.
 * Starts all the other threads then puts itself to sleep.
 */
int main(void) {
    halInit();
    chSysInit();
    chRegSetThreadName("Main");

    /* Start the heartbeat thread so it will be resetting the watchdog. */
    chThdCreateStatic(waThreadHB, sizeof(waThreadHB), LOWPRIO,
                      ThreadHeartbeat, NULL);

    /* Configure and enable the watchdog timer, stopped in debug halt. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    IWDG->KR = 0x5555;
    IWDG->PR = 3;
    IWDG->KR = 0xCCCC;

    /* Various module initialisation */
    state_estimation_init();
    dma_mutexes_init();

    /* Read config from SD card and wait for completion. */
    Thread* cfg_tp = chThdCreateStatic(waConfig, sizeof(waConfig),
                                       NORMALPRIO, config_thread, NULL);
    while(cfg_tp->p_state != THD_STATE_FINAL) chThdSleepMilliseconds(10);
    if(conf.location == CFG_M2FC_BODY)
        LocalStatus = &M2FCBodyStatus;
    else if(conf.location == CFG_M2FC_NOSE)
        LocalStatus = &M2FCNoseStatus;
    if(conf.config_loaded)
        m2status_config_status(STATUS_OK);
    else
        m2status_config_status(STATUS_ERR);

    /* Activate the EXTI pin change interrupts */
    extStart(&EXTD1, &extcfg);

    /* Start module threads */
    m2serial_shell = m2fc_shell_run;
    M2SerialSD = &SD1;
    sdStart(M2SerialSD, NULL);
    chThdCreateStatic(waM2Serial, sizeof(waM2Serial), HIGHPRIO,
                      m2serial_thread, NULL);

    chThdCreateStatic(waM2Status, sizeof(waM2Status), HIGHPRIO,
                      m2status_thread, NULL);

    chThdCreateStatic(waDatalogging, sizeof(waDatalogging), HIGHPRIO,
                      datalogging_thread, NULL);

    chThdCreateStatic(waMission, sizeof(waMission), NORMALPRIO,
                      mission_thread, NULL);

    chThdCreateStatic(waMS5611, sizeof(waMS5611), NORMALPRIO,
                      ms5611_thread, NULL);

    chThdCreateStatic(waADXL345, sizeof(waADXL345), NORMALPRIO,
                      adxl345_thread, NULL);

    chThdCreateStatic(waADXL375, sizeof(waADXL375), NORMALPRIO,
                      adxl375_thread, NULL);

    chThdCreateStatic(waPyros, sizeof(waPyros), NORMALPRIO,
                      pyro_continuity_thread, NULL);

    if(conf.use_gyro) {
        chThdCreateStatic(waL3G4200D, sizeof(waL3G4200D), NORMALPRIO,
                          l3g4200d_thread, NULL);
    } else {
        m2status_gyro_status(STATUS_OK);
    }

    if(conf.use_magno) {
        chThdCreateStatic(waHMC5883L, sizeof(waHMC5883L), NORMALPRIO,
                          hmc5883l_thread, NULL);
    } else {
        m2status_magno_status(STATUS_OK);
    }

    if(conf.use_adc) {
        chThdCreateStatic(waAnalogue, sizeof(waAnalogue), NORMALPRIO,
                          analogue_thread, NULL);
    } else {
        m2status_adc_status(STATUS_OK);
    }

    /* Let the main thread idle now. */
    chThdSetPriority(LOWPRIO);
    chThdSleep(TIME_INFINITE);

    return 0;
}
