/*
 * L3G4200D Driver
 * Cambridge University Spaceflight
 *
 * In EXTI config, must call wakeup function on DRDY interrupt.
 */

#include <stdlib.h>
#include <stdbool.h>
#include "l3g4200d.h"
#include "datalogging.h"
#include "m2status.h"

#define L3G4200D_I2C_ADDR   0x69
#define L3G4200D_I2CD       I2CD2

/* Register Addresses */
#define L3G4200D_RA_WHO_AM_I        0x0F
#define L3G4200D_RA_CTRL_REG1       0x20
#define L3G4200D_RA_CTRL_REG2       0x21
#define L3G4200D_RA_CTRL_REG3       0x22
#define L3G4200D_RA_CTRL_REG4       0x23
#define L3G4200D_RA_CTRL_REG5       0x24

#define L3G4200D_RA_FIFO_CTRL_REG   0x2E
#define L3G4200D_RA_FIFO_SRC_REG    0x2F

#define L3G4200D_RA_OUT_TEMP        0x26
#define L3G4200D_RA_OUT_BURST       0xA8


/* Register description */
#define L3G4200D_CR1_RATE_BIT       7
#define L3G4200D_CR1_RATE_LENGTH    2
#define L3G4200D_CR1_BW_BIT         5
#define L3G4200D_CR1_BW_LENGTH      2
#define L3G4200D_CR1_PD_ENABLE_BIT  3
#define L3G4200D_CR1_X_ENABLE_BIT   2
#define L3G4200D_CR1_Y_ENABLE_BIT   1
#define L3G4200D_CR1_Z_ENABLE_BIT   0

#define L3G4200D_CR3_DRDY_ENABLE_BIT    3
#define L3G4200D_CR3_WTM_ENABLE_BIT     2 /* FIFO watermark interrupt */
#define L3G4200D_CR3_OF_ENABLE_BIT      1 /* FIFO overflow interrupt */
#define L3G4200D_CR3_EMPTY_ENABLE_BIT   0 /* FIFO empty interrupt */

#define L3G4200D_CR4_DATA_MODE_BIT      7
#define L3G4200D_CR4_BLE_BIT            6 /* 0: Data LSB @ lower address */
#define L3G4200D_CR4_SCALE_BIT          5
#define L3G4200D_CR4_SCALE_LENGTH       2
#define L3G4200D_CR4_SELF_TEST_BIT      2
#define L3G4200D_CR4_SELF_TEST_LENGTH   2

#define L3G4200D_CR5_FIFO_ENABLE_BIT    6

#define L3G4200D_FIFO_CTRL_FM_BIT       7
#define L3G4200D_FIFO_CTRL_FM_LENGTH    3
#define L3G4200D_FIFO_CTRL_WTM_BIT      4
#define L3G4200D_FIFO_CTRL_WRM_LENGTH   5

#define L3G4200D_DATA_RATE_100HZ        0x01
#define L3G4200D_DATA_RATE_200HZ        0x02
#define L3G4200D_DATA_RATE_400HZ        0x03
#define L3G4200D_DATA_RATE_800HZ        0x04

#define L3G4200D_SCALE_250DPS           0x00
#define L3G4200D_SCALE_500DPS           0x01
#define L3G4200D_SCALE_2000DPS          0x02

#define L3G4200D_DATA_MODE_CONTINUOUS   0x00
#define L3G4200D_DATA_MODE_BLOCK        0x01

#define L3G4200D_FIFO_MODE_BYPASS       0x00
#define L3G4200D_FIFO_MODE_FIFO         0x01
#define L3G4200D_FIFO_MODE_STREAM       0x02
#define L3G4200D_FIFO_MODE_STF          0x03 /* Stream To FIFO */
#define L3G4200D_FIFO_MODE_BTS          0x04 /* Bypass to Stream */


/*
 * TODO:
 *
 * Quickly turn led on and off
 * GPIOA, GPIOA_LED_SENSORS
 *
 *
 */



static BinarySemaphore bsGyro;

static const I2CConfig i2cconfig = {
    OPMODE_I2C, 100000, STD_DUTY_CYCLE
};

/* Generic nicely done write function. */
static bool l3g4200d_writeRegister(uint8_t address, uint8_t data) {
    uint8_t buffer[2];
    buffer[0] = address;
    buffer[1] = data;

    msg_t rv;

    /* Transmit message */
    rv = i2cMasterTransmitTimeout(&L3G4200D_I2CD, L3G4200D_I2C_ADDR, buffer,
                                  2, NULL, 0, 1000);

    if (rv == RDY_OK) {
        return true;
    } else {
        i2cflags_t errs = i2cGetErrors(&L3G4200D_I2CD);
        (void)errs;
        return false;
    }
}

/* When called, will read 6 bytes of the XYZ data into
 * buf_data: [XL][XH][YL][YH][ZL][ZH]
 */
static bool l3g4200d_receive(uint8_t *buf_data)
{
    msg_t rv;
    uint8_t address = L3G4200D_RA_OUT_BURST;

    rv = i2cMasterTransmitTimeout(&L3G4200D_I2CD, L3G4200D_I2C_ADDR,
                                  &address, 1, buf_data, 6, 1000);

    return rv == RDY_OK;
}

/* Initialise the settings for the gyro. */
static bool l3g4200d_init(void)
{
    bool success = true;

    /* CTRL_REG2: Highpass filter mode, cut-off freq.
     * Send 00100000 [00][10 = normal mode][0000 = highest cut-off]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_CTRL_REG2, 0x20);

    /* CTRL_REG3: Interrupt configurations.
     * Send 00001000 [0000 = not needed][1 = DRDY enable]
     *               [000 = FIFO watermark, overrun, empty]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_CTRL_REG3, 0x08);

    /* CTRL_REG4: Data config and self-test.
     * Send 00010000 [0 = cont update][0 = L.Endian][01 = +-500dps max]
     *               [x][00 = normal non-test mode]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_CTRL_REG4, 0x10);

    /* CTRL_REG5: FIFO configuration.
     * Send 01000000 [0 = normal mode][1 = enable FIFO]
     *               [x][00000 = not using these]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_CTRL_REG5, 0x40);

    /* FIFO_CTRL_REG: FIFO mode configuration.
     * Send 00000000 [000 = Bypass mode][xxxxx = watermark level for interrupt]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_FIFO_CTRL_REG, 0x00);

    /* CTRL_REG1: Datarate, Filter Bandwidth, Enable each axis
     * Send 11111111 [11 = 800Hz samp][11 = 110Hz filter]
     *               [1111 = enable all axis]
     */
    success &= l3g4200d_writeRegister(L3G4200D_RA_CTRL_REG1, 0xFF);

    return success;
}

/* Checks the ID of the Gyro to ensure that we're actually talking to the Gyro
 * and not some other component. */
bool l3g4200d_ID_check(void) {
    uint8_t id_reg = L3G4200D_RA_WHO_AM_I;
    uint8_t buf[1];
    if (i2cMasterTransmitTimeout(&L3G4200D_I2CD, L3G4200D_I2C_ADDR, &id_reg,
                                 1, buf, 1, 1000) == RDY_OK) {
        return buf[0] == 0xD3;
    } else {
        return false;
    }
}

/* Conversion to meaningful units. */
static void l3g4200d_rotation_convert(uint16_t x, uint16_t y, uint16_t z,
                                      float rotation[])
{
    /* 8.75mdps/LSB for 250dps max.
       17.5mdps/LSB for 500dps max.
       70.0mdps/LSB for 2000pds max. */
    static float sensitivity = 17.5f;
    rotation[0] = (float)x * sensitivity;
    rotation[1] = (float)y * sensitivity;
    rotation[2] = (float)z * sensitivity;
}


/*
 * Interrupt handler- wake up when DRDY deasserted
 * Relevant pin needs defining in main/config
 */
void l3g4200d_wakeup(EXTDriver *extp, expchannel_t channel)
{
    (void)extp;
    (void)channel;
    chSysLockFromIsr();
    /*if(tpL3G4200D != NULL && tpL3G4200D->p_state != THD_STATE_READY) {*/
        /*chSchReadyI(tpL3G4200D);*/
    /*} else {*/
        /*m2status_gyro_status(STATUS_ERR_CALLBACK_WHILE_ACTIVE);*/
    /*}*/
    /*tpL3G4200D = NULL;*/
    chBSemSignalI(&bsGyro);
    chSysUnlockFromIsr();
}

/* Thread that runs all the stuff. */
msg_t l3g4200d_thread(void *arg)
{
    (void)arg;
    uint8_t buf_data[8];
    float rotation[3];

    m2status_gyro_status(STATUS_WAIT);
    chRegSetThreadName("L3G4200D");
    chBSemInit(&bsGyro, true);

    i2cStart(&L3G4200D_I2CD, &i2cconfig);

    while (!l3g4200d_ID_check()) {
        m2status_gyro_status(STATUS_ERR_INVALID_DEVICE_ID);
        chThdSleepMilliseconds(500);
    }

    /* Initialise the settings. */
    while (!l3g4200d_init()) {
        m2status_gyro_status(STATUS_ERR_INITIALISING);
        chThdSleepMilliseconds(500);
    }

    while (true) {
        /* Sleep until DRDY */
        chSysLock();
        /*tpL3G4200D = chThdSelf();*/
        /*chSchGoSleepTimeoutS(THD_STATE_SUSPENDED, 100);*/
        /*tpL3G4200D = NULL;*/
        chBSemWaitTimeoutS(&bsGyro, 100);
        chSysUnlock();
        m2status_gyro_status(STATUS_OK);

        /* Clears SENSOR LED */
        palClearPad(GPIOA, GPIOA_LED_SENSORS);

        /* Pull data from the gyro into buf_data. */
        if (l3g4200d_receive(buf_data)) {
            uint16_t x = buf_data[0]<<8 | buf_data[1];
            uint16_t y = buf_data[2]<<8 | buf_data[3];
            uint16_t z = buf_data[4]<<8 | buf_data[5];
            l3g4200d_rotation_convert(x, y, z, rotation);
            log_i16(M2T_CH_IMU_GYRO, x, y, z, 0);
            m2status_set_gyro(x, y, z);

            /*Set LED to show that everything is in order */
            palSetPad(GPIOA, GPIOA_LED_SENSORS);

        } else {
            m2status_gyro_status(STATUS_ERR_READING);
        }
    }
}
