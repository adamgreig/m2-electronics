/*
 * Radio transmission controller
 * M2R
 * 2014 Adam Greig, Cambridge University Spaceflight
 */

#include "radio.h"
#include "hal.h"
#include "chprintf.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dispatch.h"
#include "audio_data.h"

#define PI 3.14159f
#define BAUD (100.0f)
#define T_BIT (1.0f / BAUD)
#define FS (8000.0f)
#define SAMPLES_PER_BIT 80 //T_BIT * FS
#define F_MARK  (1000.0f)
#define F_SPACE (1350.0f)
#define MSGLEN 128
static uint8_t mark_buf[SAMPLES_PER_BIT];
static uint8_t space_buf[SAMPLES_PER_BIT];
static uint8_t zero_buf[SAMPLES_PER_BIT];

static void radio_generate_buffers()
{
    uint32_t i;
    for(i=0; i<SAMPLES_PER_BIT; i++) {
        float t = ((float)i / (float)SAMPLES_PER_BIT) * T_BIT;
        float mark  = sinf(2.0f * PI * F_MARK * t);
        float space = sinf(2.0f * PI * F_SPACE * t);
        mark_buf[i]  = (uint8_t)((mark  * 127.0f) + 127.0f);
        space_buf[i] = (uint8_t)((space * 127.0f) + 127.0f);
        zero_buf[i] = 0;
    }
}

static void radio_make_telem_string(char* buf, size_t len)
{
    chsnprintf(buf, len,
             " AD6AM AD6AM AD6AM %02d:%02d:%02d %.5f,%.5f (%d, %d) "
             "%dm (%dm) %dm/s (%dm/s) %u\n",
             m2r_state.hour, m2r_state.minute, m2r_state.second,
             (float)m2r_state.lat*1e-7f, (float)m2r_state.lng*1e-7f,
             m2r_state.gps_valid,
             m2r_state.gps_num_sats, (int)m2r_state.imu_height,
             (int)m2r_state.imu_max_height, (int)m2r_state.imu_velocity,
             (int)m2r_state.imu_max_velocity, m2r_state.fc_state);
}

uint8_t *radio_fm_sampbuf;
uint16_t radio_fm_sampidx = 0;
uint16_t radio_fm_samplen = 1;
uint8_t radio_fm_rtty = 1;
uint8_t radio_fm_bitidx = 0;
uint8_t radio_fm_bitbuf[11];
uint8_t radio_fm_byteidx = 0;
uint8_t radio_fm_bytebuf[MSGLEN];
uint8_t radio_fm_audioidx = 0;
uint8_t* radio_fm_audioqueue[16];
uint16_t radio_fm_audioqueuelens[16];
static void radio_fm_timer(GPTDriver *gptd)
{
    uint8_t i, byte;
    (void)gptd;
    /* If we just wrote the last sample... */
    if(radio_fm_sampidx >= radio_fm_samplen) {
        radio_fm_sampidx = 0;

        palTogglePad(GPIOB, GPIOB_LED_RADIO);

        /* If we're transmitting RTTY... */
        if(radio_fm_rtty) {

            radio_fm_bitidx++;
            /* If we're now also transmitted all bits... */
            if(radio_fm_bitidx == 11) {
                radio_fm_bitidx = 0;
                radio_fm_byteidx++;
                if(radio_fm_bytebuf[radio_fm_byteidx] == 0x00) {
                    /* End of message */
                    radio_fm_byteidx = 0;
                    radio_make_telem_string(radio_fm_bytebuf, 128);
                    radio_fm_rtty = 0;
                    radio_fm_audioidx = 0;

                } else {
                    /* START */
                    radio_fm_bitbuf[0] = 0;
                    /* Data ASCII7 */
                    byte = radio_fm_bytebuf[radio_fm_byteidx];
                    for(i=0; i<7; i++)
                        radio_fm_bitbuf[i + 1] = (byte & (1 << i)) >> i;
                    /* STOPs */
                    radio_fm_bitbuf[8] = 1;
                    radio_fm_bitbuf[9] = 1;
                    radio_fm_bitbuf[10] = 1;
                }
            }

            /* Set new bit buffer */
            if(radio_fm_bitbuf[radio_fm_bitidx] == 1)
                radio_fm_sampbuf = mark_buf;
            else if(radio_fm_bitbuf[radio_fm_bitidx] == 2)
                radio_fm_sampbuf = zero_buf;
            else if(radio_fm_bitbuf[radio_fm_bitidx] == 0)
                radio_fm_sampbuf = space_buf;
            radio_fm_samplen = SAMPLES_PER_BIT;

        /* If we're transmitting audio... */
        } else {
            /* Need to load next audio file to play. */
            if(radio_fm_audioqueue[radio_fm_audioidx] == 0x00) {
                /* End of message */
                radio_fm_sampidx = 65534;
                radio_fm_audioidx = 0;
                say_altitude(m2r_state.imu_height,
                             &radio_fm_audioqueue,
                             &radio_fm_audioqueuelens);
                radio_fm_rtty = 1;
            }

            radio_fm_sampbuf = radio_fm_audioqueue[
                radio_fm_audioidx];
            radio_fm_samplen = radio_fm_audioqueuelens[
                radio_fm_audioidx];
            radio_fm_audioidx++;
        }
    }

    /* Write next sample to the DAC */
    DAC->DHR8R2 = radio_fm_sampbuf[radio_fm_sampidx];
    radio_fm_sampidx++;

}

uint8_t radio_ssb_bitidx = 0;
uint8_t radio_ssb_bitbuf[10];
uint8_t radio_ssb_byteidx = 0;
uint8_t radio_ssb_bytebuf[MSGLEN];
static void radio_ssb_timer(GPTDriver *gptd)
{
    uint8_t i, byte;
    (void)gptd;

    if(radio_ssb_bitbuf[radio_ssb_bitidx]) {
        DAC->DHR8R2 = 100;
    } else {
        DAC->DHR8R2 = 183;
    }

    radio_ssb_bitidx++;

    if(radio_ssb_bitidx == 10) {
        radio_ssb_bitidx = 0;
        radio_ssb_byteidx++;
        if(radio_ssb_bytebuf[radio_ssb_byteidx] == 0x00) {
            /* End of message */
            radio_ssb_byteidx = 0;
            radio_make_telem_string(radio_ssb_bytebuf, 128);
        } else {
            /* START bit */
            radio_ssb_bitbuf[0] = 1;
            /* Data bits, 7bit ASCII */
            byte = radio_ssb_bytebuf[radio_ssb_byteidx];
            for(i=0; i<7; i++)
                radio_ssb_bitbuf[i + 1] = byte & (1<<i);
            /* Stop bits */
            radio_ssb_bitbuf[8] = 1;
            radio_ssb_bitbuf[9] = 1;
        }
    }
}

void radio_say(u8* buf, u16 len)
{
    radio_fm_audioqueue[0] = buf;
    radio_fm_audioqueue[1] = 0;
    radio_fm_audioqueuelens[0] = len;
    radio_fm_rtty = 0;
}

static char* psk_buf;
static int psk_buf_bit;
static size_t psk_buf_len;
static psk_cb(GPTDriver* gptd)
{
    static uint8_t last_bit = 0;
    uint8_t this_bit;
    if(psk_buf_bit/8 == psk_buf_len) {
        chSysLockFromIsr();
        gptStopTimerI(gptd);
        chSysUnlockFromIsr();
        psk_buf_bit = 0;
        return;
    }
    this_bit = (psk_buf[psk_buf_bit/8] >> (7 - (psk_buf_bit % 8))) & 1;
    if(this_bit != last_bit)
        (&PWMD3)->tim->CR1 |= TIM_CR1_OPM | TIM_CR1_CEN;
    psk_buf_bit++;
    last_bit = this_bit;
}

static const GPTConfig gptcfg1 = {
    32000,
    radio_fm_timer,
    0
};

static const GPTConfig gptcfg_psk = {
    1000000,
    psk_cb
};

static const PWMConfig pwmcfg = {
    /* PWM clock: 4MHz */
    4000000,
    /* PWM period: 400 cycles * 4MHz = 100µs -> 10kHz */
    400,
    NULL,
    {
        {PWM_OUTPUT_DISABLED, NULL},
        {PWM_OUTPUT_ACTIVE_LOW, NULL},
        {PWM_OUTPUT_DISABLED, NULL},
        {PWM_OUTPUT_DISABLED, NULL}
    },
    0
};

static const random_msg[] = {
0x0B, 0x52, 0x36, 0xCB, 0x8B, 0xF2, 0x2C, 0xF6, 0xA7, 0x85, 0xAF, 0xDE, 0xB9,
0x08, 0x63, 0x68, 0xFB, 0x31, 0x83, 0x0A, 0x4B, 0x15, 0x82, 0x1B, 0x45, 0x63,
0xEC, 0x0F, 0x88, 0x4C, 0x9D, 0x78, 0x17, 0x29, 0x67, 0x94, 0xD8, 0x3B, 0xC6,
0x88, 0xF5, 0x03, 0x03, 0x32, 0x36, 0x7F, 0xBE, 0x84, 0x66, 0x73, 0xBB, 0xB1,
0x22, 0xE6, 0x5C, 0xBE, 0xC6, 0x9E, 0x17, 0xD0, 0x8B, 0x61, 0x7C, 0x08, 0x32,
/*0x0F, 0x2D, 0x02, 0x66, 0xA9, 0x20, 0xBC, 0x22, 0xF1, 0x61, 0xBA, 0x9B, 0x4A,*/
/*0x7B, 0x61, 0x7C, 0x14, 0x01, 0xB1, 0x64, 0x0F, 0x23, 0x07, 0xC9, 0x0C, 0x58,*/
/*0x7A, 0x98, 0xE8, 0xCA, 0x00, 0x6E, 0x33, 0x65, 0x31, 0x9A, 0x2B, 0xC7, 0x35,*/
/*0x6C, 0x39, 0xBD, 0x92, 0x28, 0x06, 0xBF, 0xB2, 0xC3, 0x24, 0x05, 0xC0, 0x81,*/
/*0x60, 0x87, 0x3F, 0x53, 0x36, 0x1B, 0x83, 0xDA, 0x20, 0x10, 0x34, 0xBE, 0xFA,*/
/*0xDC, 0x06, 0xF2, 0x6D, 0xBD, 0x37, 0x78, 0xA7, 0x7D, 0x23, 0x20, 0xEB, 0x61,*/
/*0xC6, 0xA3, 0x4A, 0x02, 0xE8, 0xFE, 0x1D, 0x18, 0x55, 0x6D, 0xB1, 0x23, 0x34,*/
/*0x3F, 0xC0, 0x30, 0xCD, 0xC9, 0x12, 0x88, 0x7B, 0x74, 0x9A, 0x37, 0x03, 0x94,*/
/*0x8E, 0x00, 0xFB, 0x70, 0x37, 0x01, 0xC5, 0x0E, 0x05, 0xBE, 0x55, 0xB1, 0xF0,*/
/*0xEF, 0xDB, 0xF1, 0xBE, 0x0D, 0x25, 0xA0, 0xDE, 0x85, 0x80, 0x3E, 0xCE, 0x1C,*/
/*0x67, 0x83, 0xBB, 0xFD, 0x2C, 0xA1, 0x17, 0xDB, 0x29, 0xFC, 0x6E, 0x2D, 0x63,*/
/*0x3F, 0x03, 0xF1, 0xEF, 0x1D, 0xE5, 0xCB, 0x31, 0x24, 0xC2, 0xC3, 0x20, 0x55,*/
/*0x53, 0x7B, 0xE7, 0x02, 0xBE, 0x36, 0xB4, 0x63, 0x46, 0xC6, 0x94, 0x44, 0xAC,*/
/*0xFA, 0x0A, 0x52, 0xFA, 0x94, 0x89, 0x42, 0x1D, 0xAD, 0x4D, 0xC4, 0xE2, 0xA2,*/
/*0xD3, 0xA7, 0x34, 0x64, 0x4B, 0x25, 0xC8, 0xDC, 0x50, 0xA3, 0x12, 0x73, 0x71,*/
/*0x44, 0x75, 0x2A, 0x62, 0x6B, 0x75, 0x15, 0xA2, 0xA4, 0xB9, 0x63, 0x12, 0xDC,*/
/*0xD3, 0x0F, 0xCC, 0xC2, 0xF5, 0x5D, 0x02, 0xF0, 0x32, 0x64, 0x3A, 0x65, 0x79,*/
/*0xDB, 0xB6, 0xB4, 0x36, 0xAC, 0x3B, 0xE4, 0x37, 0x95, 0x22, 0xC5, 0x76, 0x51,*/
/*0xCB, 0x31, 0x90, 0x15, 0x4A, 0x0C, 0xDA, 0xCD, 0x60, 0x28, 0x15, 0x59, 0x72,*/
/*0x28, 0x19, 0x27, 0xF4, 0x4C, 0x0D, 0xD6, 0xFE, 0xE0, 0xAA, 0x70, 0x2F, 0xF2,*/
/*0xE9, 0x9B, 0x00, 0x39, 0x2B, 0x2C, 0xA4, 0xE0, 0xF8, 0x62, 0x9F, 0x4D, 0x1B,*/
/*0xCD, 0x6C, 0x5E, 0x6D, 0x18, 0xC4, 0xD0, 0xA4, 0x68, 0xA3, 0x8E, 0x74, 0xEC,*/
/*0xC7, 0x02, 0x17, 0x53, 0x4E, 0x46, 0x30, 0x60, 0x54, 0x52, 0x6F, 0xFA, 0xFE,*/
/*0x1D, 0x9A, 0x47, 0xB2, 0xDD, 0x0B, 0x58, 0x8B, 0xCC, 0x7E, 0xD2, 0xE5, 0xE7,*/
/*0xB9, 0xDD, 0x3E, 0xA3, 0xB8, 0x3B, 0xF9, 0x11, 0x68, 0xDF, 0xF2, 0x32, 0xCC,*/
/*0x39, 0xBD, 0x85, 0x82, 0x1C, 0xC5, 0xFE, 0xD8, 0x27, 0x8A, 0x4D, 0x40, 0x3E,*/
/*0x36, 0x0A, 0x0C, 0x91, 0x92, 0x72, 0xFE, 0xA5, 0x14, 0x60, 0xF1, 0x6F, 0xCC,*/
/*0xB9, 0x17, 0xB6, 0x6D, 0x04, 0xB7, 0xCE, 0x20, 0x3D, 0xE9, 0xDB, 0x03, 0x43,*/
/*0xA4, 0x31, 0xFE, 0x96, 0x27, 0xB3, 0x2E, 0x00, 0xDE, 0xDA, 0x95, 0xCD, 0xBE,*/
/*0x53, 0xA8, 0x66, 0xF7, 0xFE, 0x6B, 0x70, 0xB9, 0xE5, 0x31, 0xFC, 0xA7, 0x4B,*/
/*0xF5, 0x3F, 0xDB, 0x09, 0x8F, 0x9F, 0x06, 0xEC, 0x9A, 0x97, 0x95, 0x29, 0x35,*/
/*0x08, 0xD9, 0x8A, 0x5C, 0xEE, 0xD7, 0x15, 0x5F, 0xEB, 0x4F, 0xEE, 0xA5, 0x84,*/
/*0xB8, 0x93, 0x9D, 0xEC, 0xD0, 0x95, 0x0B, 0xCF, 0xFA, 0x38, 0xA9, 0xDF, 0xF4,*/
/*0xE8, 0x9F, 0xD7, 0xFD, 0xAA, 0xEA, 0xF0, 0x6E, 0xD8, 0x47, 0x6C, 0xD9, 0xEB,*/
/*0x47, 0x08, 0x06, 0x5E, 0xEC, 0xFB, 0x3C, 0xB0, 0x89, 0x08, 0xF7, 0xEA, 0xB1,*/
/*0xA7, 0x3A, 0x37, 0xEE, 0xEA, 0xFA, 0xE6, 0xD3, 0xD3, 0x54, 0x20, 0x4C, 0x42,*/
/*0xA9, 0x6D, 0x57, 0xA4, 0x32, 0xC1, 0xD4, 0xE6, 0xE8, 0x96, 0xCF, 0xBB, 0xEC,*/
/*0x5E, 0x90, 0xB4, 0x1F, 0x50, 0x4C, 0x3B, 0x93, 0x75, 0xBE, 0xC5, 0x75, 0x7D,*/
/*0x18, 0x1E, 0x45, 0x9F, 0x37, 0x31, 0x08, 0xD7, 0x0E, 0xAC, 0xEC, 0x08, 0xFB,*/
/*0xC3, 0x13, 0x10, 0x7F, 0xB4, 0xEF, 0xAC, 0x6D, 0x4A, 0x82, 0x4D, 0x33, 0x7B,*/
/*0x31, 0x6C, 0xCF, 0x39, 0x8E, 0x00, 0x49, 0xFD, 0x7D, 0xA7, 0x54, 0xD3, 0x69,*/
/*0x50, 0xBA, 0x9E, 0x95, 0xA5, 0xEE, 0x81, 0x23, 0x69, 0x57, 0x22, 0x7D, 0x2E,*/
/*0xE6, 0xE0, 0x05, 0x12, 0xDB, 0x10, 0xE3, 0xF1, 0x7C, 0xB4, 0x5A, 0xF7, 0x35,*/
/*0xA0, 0xD7, 0x33, 0xAF, 0x81, 0x51, 0x9A, 0x14, 0x7A, 0x27, 0x19, 0x73, 0x19,*/
/*0x64, 0xB3, 0x53, 0x51, 0xAB, 0xB4, 0x9B, 0xA7, 0x9D, 0x88, 0x1A, 0x86, 0x7A,*/
/*0x84, 0xBC, 0x5C, 0x09, 0xA8, 0x92, 0xF6, 0x7F, 0x7F, 0xBC, 0xFC, 0xBE, 0x4B,*/
/*0xDB, 0xAD, 0x5A, 0x0C, 0x33, 0x16, 0x27, 0x7B, 0x41, 0xA3, 0x66, 0xE1, 0x4A,*/
/*0xC8, 0x57, 0x46, 0x41, 0xE6, 0xC8, 0x5B, 0x86, 0x20, 0x01, 0xF7, 0x05, 0x33,*/
/*0xBF, 0xA8, 0x47, 0x6C, 0xA7, 0xF9, 0x94, 0xB9, 0x64, 0xEF, 0x93, 0xD6, 0x91,*/
/*0x88, 0xD1, 0x12, 0xA2, 0xBF, 0x3D, 0xF6, 0x7D, 0x3A, 0x57, 0x1B, 0x92, 0x40,*/
/*0x4A, 0xD5, 0x3B, 0x31, 0x78, 0x20, 0xCD, 0x29, 0x18, 0x83, 0xF7, 0x17, 0xD2,*/
/*0x99, 0x6C, 0xBD, 0x88, 0xF6, 0xE6, 0x3F, 0x26, 0xEE, 0xB4, 0x99, 0x4A, 0xE1,*/
/*0x1C, 0xF3, 0x08, 0xBE, 0x33, 0x66, 0xE8, 0xB7, 0x0F, 0x4E, 0x91, 0xB1, 0x9D,*/
/*0xF3, 0x88, 0x17, 0x8D, 0xB6, 0xCC, 0x99, 0x28, 0x9B, 0xC2, 0xD0, 0x33, 0x69,*/
/*0x25, 0xCA, 0x2B, 0x89, 0xBF, 0x8C, 0xAE, 0xA2, 0x99, 0x99, 0x1D, 0xAD, 0x21,*/
/*0x9E, 0xB4, 0x4F, 0x7C, 0x0C, 0x8A, 0x41, 0xED, 0x87, 0x43, 0x45, 0x6A, 0x94,*/
/*0x05, 0x8B, 0x3F, 0x4B, 0x34, 0x9A, 0x08, 0x08, 0x50, 0xF5, 0x89, 0xB5, 0x23,*/
/*0x7E, 0xF9, 0x28, 0xD4, 0xC4, 0xF4, 0x38, 0xF2, 0xA6, 0xFD, 0xE5, 0x5E, 0x17,*/
/*0x5C, 0xE3, 0x4A, 0x56, 0x27, 0xF0, 0xF7, 0xBF, 0x1E, 0x92, 0x0A, 0xD5, 0x34,*/
/*0xA9, 0x85, 0xA1, 0x66, 0xDE, 0x47, 0x58, 0xF3, 0x0B, 0xDB, 0x6C, 0x65, 0x95,*/
/*0xD7, 0xC7, 0x26, 0xEC, 0x3B, 0xC7, 0x7A, 0x6B, 0x9D, 0x47, 0xA0, 0x0C, 0x88,*/
/*0x89, 0x13, 0xF0, 0x4A, 0xAF, 0x0C, 0xB1, 0xC7, 0xE9, 0x6E, 0x3F, 0xC9, 0x0F,*/
/*0x3E, 0xE7, 0x55, 0x40, 0x0E, 0x1D, 0xF4, 0xDD, 0x6F, 0xB3, 0x11, 0x9F, 0x35,*/
/*0x37, 0x3E, 0x7B, 0x3D, 0x46, 0x0F, 0x61, 0x14, 0x01, 0xB9, 0x89, 0x0F, 0x18,*/
/*0xE9, 0x23, 0x3C, 0x98, 0x65, 0x93, 0x41, 0x79, 0xAB, 0x3B, 0x00, 0x85, 0xE5,*/
/*0x1C, 0xB5, 0xBA, 0xB6, 0x38, 0x53, 0x0B, 0xC1, 0x31, 0x2C, 0x2B, 0x1C, 0x52,*/
/*0x7F, 0xCF, 0x9E, 0xC0, 0x4B, 0xF5, 0xFA, 0x73, 0xEF, 0x30, 0x98, 0xA1, 0x70,*/
/*0xC8, 0xFA, 0x00, 0xCF, 0xAA, 0xF6, 0x84, 0xA0, 0xD1, 0xCD, 0xE8, 0x31, 0xAB,*/
/*0xEA, 0xFD, 0x5F, 0x5A, 0xF9, 0x78, 0x0B, 0xA8, 0xE9, 0x14, 0xBA, 0x0F, 0x1D,*/
/*0x58, 0x63, 0xC3, 0x22, 0x49, 0x9B, 0x59, 0x70, 0x9A, 0xF6, 0xC7, 0x18, 0x0C,*/
/*0x1A, 0x58, 0xC6, 0x01, 0xF2, 0x01, 0xD0, 0x6C, 0x17, 0x03, 0xB2, 0xF3, 0xF4,*/
/*0xA7, 0x76, 0x77, 0x6B, 0x81, 0xCB, 0xE0, 0x97, 0xCF, 0xFD, 0xA3, 0xEB, 0x06,*/
/*0x41, 0x11, 0xD5, 0x3A, 0xE1, 0xE8, 0x4F, 0x1C, 0xB4, 0xD3, 0xD5, 0xAB, 0xA6,*/
/*0x84, 0xF8, 0x0F, 0xE5, 0x87, 0x9B, 0x1F, 0x3B, 0xFD, 0xE1*/
};


msg_t radio_thread(void* arg)
{
    char mymsg[] = "\x2D\x55" "AD6AM AD6AM TESTING TESTING, HELLO PSK";
    int i, this_bit, last_bit = 0;

    chRegSetThreadName("Radio");
    palSetPad(GPIOB, GPIOB_MTX2_EN);

    /* Start PWM with config */
    pwmStart(&PWMD3, &pwmcfg);
    /* Immediately stop the PWM timer */
    (&PWMD3)->tim->CR1 &= ~TIM_CR1_CEN;
    (&PWMD3)->tim->CNT = 0;
    /* Configure channel 1 to 205 cycles -> 51.25µs delay */
    pwmEnableChannel(&PWMD3, 1, 205);

    psk_buf = random_msg;
    psk_buf_bit = 0;
    psk_buf_len = sizeof(random_msg);

    gptStart(&GPTD2, &gptcfg_psk);
    gptStopTimer(&GPTD2);

    while(true) {
        if((&GPTD2)->state != GPT_CONTINUOUS) {
            palClearPad(GPIOB, GPIOB_LED_RADIO);
            chThdSleepMilliseconds(1000);
            palSetPad(GPIOB, GPIOB_LED_RADIO);
            gptStartContinuous(&GPTD2, 500);
        } else {
            chThdSleepMilliseconds(1);
        }
    }

    while(TRUE) {
        palSetPad(GPIOB, GPIOB_LED_RADIO);
        for(i=0; i<sizeof(mymsg)*8; i++) {
            this_bit = (mymsg[i/8] >> (7 - (i%8))) & 1;
            if(this_bit != last_bit) {
                /* Turn the timer on in one-pulse mode. */
                (&PWMD3)->tim->CR1 |= TIM_CR1_OPM | TIM_CR1_CEN;
            }
            last_bit = this_bit;
            chThdSleepMilliseconds(2);
        }
        if(last_bit) {
            (&PWMD3)->tim->CR1 |= TIM_CR1_OPM | TIM_CR1_CEN;
        }
        palClearPad(GPIOB, GPIOB_LED_RADIO);
        chThdSleepMilliseconds(2000);
    }

    /* Compute the sine waves for AFSK */
    radio_generate_buffers();

    chsnprintf(radio_fm_bytebuf, MSGLEN, "AD6AM AD6AM MARTLET 2 FM INITIALISE ");

    /* Enable DAC */
    RCC->APB1ENR |= (1<<29);
    DAC->CR = DAC_CR_EN2 | DAC_CR_BOFF2;

    /*while(!m2r_state.armed)*/
        /*chThdSleepMilliseconds(100);*/

    say_altitude(m2r_state.imu_height,
                 &radio_fm_audioqueue,
                 &radio_fm_audioqueuelens);


    /* Enable 8kHz FM Radio timer */
    /*gptStart(&GPTD2, &gptcfg1);*/
    /*gptStartContinuous(&GPTD2, 4);*/

    while(TRUE) {
        chThdSleepMilliseconds(100);
    }
}
