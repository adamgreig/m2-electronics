
/*
 * ADC Driver (ADC1, ADC2, ADC3)
 * M2FC
 * 2015 Eivind Roson Eide, Cambridge University Spaceflight
 */

#include <stdlib.h>
#include "ADCs.h"
#include "microsd.h"
#include "config.h"
#include "state_estimation.h"
#include "chprintf.h"


#define ADC_NUM_CHANNELS   2 //What is a channel??
#define ADC_BUF_DEPTH      1024

#define SG1 GPIOA_STRAIN_1
#define SG2 GPIOA_STRAIN_2
#define SG3 GPIOA_STRAIN_3
#define TC1 GPIOC_THERMO_1
#define TC2 GPIOC_THERMO_2
#define TC3 GPIOC_THERMO_3

/* 
 * Configure a GPT object 
 */ 

static adcsample_t samples1[ADC_GRP_NUM_CHANNELS * ADC_GRP_BUF_DEPTH];
static adcsample_t samples2[ADC_GRP_NUM_CHANNELS * ADC_GRP_BUF_DEPTH];
static adcsample_t samples3[ADC_GRP_NUM_CHANNELS * ADC_GRP_BUF_DEPTH];

// Question?
// This is the timer clock config? Should this be set to a higher number
// and then called when it reaches a given number on line 155
static GPTConfig gpt_adc_config = 
{ 
     40000,  // timer clock: 4khz 
     gpt_adc_trigger  // Timer callback function 
};

/*
* ADC conversion group.
* Mode:        Continuous, 1024 samples of 2 channels,
* 
* What does this mean:?
*  SW triggered.
* Channels:    IN7, IN8, IN7, IN8, IN7, IN8, Sensor, VBat/2.
*/

static const ADCConversionGroup adcConGrp = {
  TRUE,
  ADC_NUM_CHANNELS,
  adccallback,
  adcerrorcallback,
  /I have no idea on what the following lines mean:/ //Question: Could you help me out here?
  0,                                                            / CFGR     /
  ADC_TR(0, 4095),                                              / TR1      /
  ADC_CCR_DUAL(1) | ADC_CCR_TSEN | ADC_CCR_VBATEN,              / CCR      /
  {                                                             / SMPR[2]  /
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_19P5)
    | ADC_SMPR1_SMP_AN8(ADC_SMPR_SMP_19P5),
    ADC_SMPR2_SMP_AN16(ADC_SMPR_SMP_61P5)
    | ADC_SMPR2_SMP_AN17(ADC_SMPR_SMP_61P5),
  },
  {                                                             / SQR[4]   
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN7)  | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN8) |
    ADC_SQR1_SQ3_N(ADC_CHANNEL_IN7)  | ADC_SQR1_SQ4_N(ADC_CHANNEL_IN8),
    ADC_SQR2_SQ5_N(ADC_CHANNEL_IN7)  | ADC_SQR2_SQ6_N(ADC_CHANNEL_IN8) |
    ADC_SQR2_SQ7_N(ADC_CHANNEL_IN16) | ADC_SQR2_SQ8_N(ADC_CHANNEL_IN17),
    0,
    0
  },
  {                                                             / SSMPR[2] /
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_19P5)
    | ADC_SMPR1_SMP_AN8(ADC_SMPR_SMP_19P5),
    ADC_SMPR2_SMP_AN16(ADC_SMPR_SMP_61P5)
    | ADC_SMPR2_SMP_AN17(ADC_SMPR_SMP_61P5),
  },
  {                                                             / SSQR[4]  /
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN8)  | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN7) |
    ADC_SQR1_SQ3_N(ADC_CHANNEL_IN8)  | ADC_SQR1_SQ4_N(ADC_CHANNEL_IN7),
    ADC_SQR2_SQ5_N(ADC_CHANNEL_IN8)  | ADC_SQR2_SQ6_N(ADC_CHANNEL_IN7) |
    ADC_SQR2_SQ7_N(ADC_CHANNEL_IN17) | ADC_SQR2_SQ8_N(ADC_CHANNEL_IN16),
    0,
    0
  }
}; 

//Question? How do I stop the sampling after finding an element? Don't think each of them have a callback functioon? 
//Should we do it linearly instead? Call this function at the given sample rate ->  
static void gpt_adc_trigger(GPTDriver *gpt_ptr) { //
    (void)gptp;
    adcStartConversion(&ADCD1, &adcConGrp, samples1, ADC_BUF_DEPTH)
    adcStartConversion(&ADCD2, &adcConGrp, samples1, ADC_BUF_DEPTH)
    adcStartConversion(&ADCD3, &adcConGrp, samples1, ADC_BUF_DEPTH)
    //chSysLockFromISR();
    //chSysUnlockFromISR();
}


//Question? Is the buffer some kind of array with type abcsample_t/ uint16_t? Or 

static void adccallback(ADCDriver *adcDriverpointer, adcsample_t *buffer, size_t n) {
    (void)adcp;
    int16_t SIZE = (int16_t)n;
    int16_t i = 0;
    int16_t j = 0;
    const int8_t MAX_j = 20;
    uint16_t bufferContent;
  
  
    while(j < n)
    {
        bufferContent = buffer[j]
        microsd_log_s16(CHAN_IMU_HGA, accels[0], accels[1], accels[2], 0);

        j = j + 2;
    }
  
  
}

static void adc_error_callback(ADCDriver *adcDriverpointer, adcerror_t err) {
  (void)adcp;
  (void)err;
}



msg_t ADC_read_SGs_and_TCs (void *args)
{
    (void) args;
    

    
    const ADCConfig adcconfig = {};
    

        
    chRegSetThreadName("ADCs");
    
        
    
    adcInit();
    adcStart(&ADCD1, &adcconfig);
    adcStart(&ADCD2, &adcconfig);
    adcStart(&ADCD3, &adcconfig);
    
    /* 
    * Start the GPT timer 
    * Timer is clocked at 1Mhz (1us). Timer triggers at 1 and calls the callback function 
    */ 
    gptStart(&GPTD1, &gpt_adc_config); 
    gptStartContinuous(&GPTD1, 1); //Is this the way to do it? 

    while(TRUE)
    {
        
    }
    
    return (msg_t)NULL;
} 














/*
#region

#region waste

* http://www.st.com/st-web-ui/static/active/en/resource/technical/document/reference_manual/DM00031020.pdf page 374
 * Position, Priority, Type of priority, Acronym, Description, Address
 * 
 * 18 |25 |settable ADC| ADC1, ADC2 and ADC3 global interrupts | 0x0000 0088
 * 
*



/*
CH_IRQ_HANDLER(myIRQ) {
  CH_IRQ_PROLOGUE();
 
  / IRQ handling code, preemptable if the architecture supports it./
 
  chSysLockFromISR();
  / Invocation of some I-Class system APIs, never preemptable./
  chSysUnlockFromISR();
 
  /More IRQ handling code, again preemptable./
 
  CH_IRQ_EPILOGUE();
}


*

#endregion

 //#######NOT CODE###########/////
 
#region Copied for referance from http://forum.chibios.org/phpbb/viewtopic.php?f=4&t=7
//// Copied for referance from http://forum.chibios.org/phpbb/viewtopic.php?f=4&t=7

// ADCConfig structure for stm32 MCUs is empty
static ADCConfig adccfg = {};

// Create buffer to store ADC results. This is
// one-dimensional interleaved array
#define ADC_BUF_DEPTH 2 // depth of buffer
#define ADC_CH_NUM 2    // number of used ADC channels
static adcsample_t samples_buf[ADC_BUF_DEPTH * ADC_CH_NUM]; // results array

// Fill ADCConversionGroup structure fields
static ADCConversionGroup adccg = {
   // this 3 fields are common for all MCUs
      // set to TRUE if need circular buffer, set FALSE otherwise
      TRUE,
      // number of channels
      (uint16_t)(ADC_CH_NUM),
      // callback function, set to NULL for begin
      NULL,
   // Resent fields are stm32 specific. They contain ADC control registers data.
   // Please, refer to ST manual RM0008.pdf to understand what we do.
      // CR1 register content, set to zero for begin
      0,
      // CR2 register content, set to zero for begin
      0,
      // SMRP1 register content, set to zero for begin
      0,
      // SMRP2 register content, set to zero for begin
      0,
      // SQR1 register content. Set channel sequence length
      ((ADC_CH_NUM - 1) << 20),
      // SQR2 register content, set to zero for begin
      0,
      // SQR3 register content. We must select 2 channels
      // For example 15th and 10th channels. Refer to the
      // pinout of your MCU to select other pins you need.
      // On STM32-P103 board they connected to PC15 and PC0 contacts
      // On STM32-103STK board they connected to EXT2-7 contact and joystick
      (15 | (10 << 5)),
};

// Thats all with configuration

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Setup pins of our MCU as analog inputs
  palSetPadMode(IOPORT3, 5, PAL_MODE_INPUT_ANALOG); // this is 15th channel
  palSetPadMode(IOPORT3, 0, PAL_MODE_INPUT_ANALOG); // this is 10th channel

  // Following 3 functions use previously created configuration
  // to initialize ADC block, start ADC block and start conversion.
  // &ADCD1 is pointer to ADC driver structure, defined in the depths of HAL.
  // Other arguments defined ourself earlier.
  adcInit();
  adcStart(&ADCD1, &adccfg);
  adcStartConversion(&ADCD1, &adccg, &samples_buf[0], ADC_BUF_DEPTH);

  // Thats all. Now your conversion run in background without assistance.

  uint16_t i = 0;

  while (TRUE) {
    i = samples_buf[0] / 2;
  }
  return 0;
}


#endregion

#region


 COPIED FROM http://forum.chibios.org/phpbb/viewtopic.php?f=2&t=939
#define ADC_GRP1_NUM_CHANNELS   2
#define ADC_GRP1_BUF_DEPTH      8

#define ADC_GRP2_NUM_CHANNELS   8
#define ADC_GRP2_BUF_DEPTH      16

static adcsample_t samples1[ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH];
static adcsample_t samples2[ADC_GRP2_NUM_CHANNELS * ADC_GRP2_BUF_DEPTH];
//uint8_t iman=100;
/
/ ADC streaming callback.
/
size_t nx = 0, ny = 0;
static void adccallback(ADCDriver *adcp, adcsample_t *buffer, size_t n) {

  (void)adcp;
  if (samples2 == buffer) {
    nx += n;
  }
  else {
    ny += n;
  }
}

static void adcerrorcallback(ADCDriver *adcp, adcerror_t err) {

  (void)adcp;
  (void)err;
}

/
* ADC conversion group.
* Mode:        Linear buffer, 8 samples of 2 channels, SW triggered.
* Channels:    IN7, IN8.
/
static const ADCConversionGroup adcgrpcfg1 = {
  FALSE,
  ADC_GRP1_NUM_CHANNELS,
  NULL,
  adcerrorcallback,
  0,                                                            / CFGR     /
  ADC_TR(0, 4095),                                              / TR1     /
  ADC_CCR_DUAL(1),                                              / CCR       /
  {                                                             / SMPR[2]  /
    0,
    0
  },
  {                                                             / SQR[4]   /
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN7) | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN8),
    0,
    0,
    0
  },
  {                                                             / SSMPR[2] /
    0,
    0
  },
  {                                                             / SSQR[4]  /
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN8) | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN7),
    0,
    0,
    0
  }
};

/
* ADC conversion group.
* Mode:        Continuous, 16 samples of 8 channels, SW triggered.
* Channels:    IN7, IN8, IN7, IN8, IN7, IN8, Sensor, VBat/2.
/
static const ADCConversionGroup adcgrpcfg2 = {
  TRUE,
  ADC_GRP2_NUM_CHANNELS,
  adccallback,
  adcerrorcallback,
  0,                                                            / CFGR     /
  ADC_TR(0, 4095),                                              / TR1      /
  ADC_CCR_DUAL(1) | ADC_CCR_TSEN | ADC_CCR_VBATEN,              / CCR      /
  {                                                             / SMPR[2]  /
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_19P5)
    | ADC_SMPR1_SMP_AN8(ADC_SMPR_SMP_19P5),
    ADC_SMPR2_SMP_AN16(ADC_SMPR_SMP_61P5)
    | ADC_SMPR2_SMP_AN17(ADC_SMPR_SMP_61P5),
  },
  {                                                             / SQR[4]   
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN7)  | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN8) |
    ADC_SQR1_SQ3_N(ADC_CHANNEL_IN7)  | ADC_SQR1_SQ4_N(ADC_CHANNEL_IN8),
    ADC_SQR2_SQ5_N(ADC_CHANNEL_IN7)  | ADC_SQR2_SQ6_N(ADC_CHANNEL_IN8) |
    ADC_SQR2_SQ7_N(ADC_CHANNEL_IN16) | ADC_SQR2_SQ8_N(ADC_CHANNEL_IN17),
    0,
    0
  },
  {                                                             / SSMPR[2] /
    ADC_SMPR1_SMP_AN7(ADC_SMPR_SMP_19P5)
    | ADC_SMPR1_SMP_AN8(ADC_SMPR_SMP_19P5),
    ADC_SMPR2_SMP_AN16(ADC_SMPR_SMP_61P5)
    | ADC_SMPR2_SMP_AN17(ADC_SMPR_SMP_61P5),
  },
  {                                                             / SSQR[4]  /
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN8)  | ADC_SQR1_SQ2_N(ADC_CHANNEL_IN7) |
    ADC_SQR1_SQ3_N(ADC_CHANNEL_IN8)  | ADC_SQR1_SQ4_N(ADC_CHANNEL_IN7),
    ADC_SQR2_SQ5_N(ADC_CHANNEL_IN8)  | ADC_SQR2_SQ6_N(ADC_CHANNEL_IN7) |
    ADC_SQR2_SQ7_N(ADC_CHANNEL_IN17) | ADC_SQR2_SQ8_N(ADC_CHANNEL_IN16),
    0,
    0
  }
}; 


#endregion
*/
#region

/* Usefull links: 
 * https://sites.google.com/site/electronicprojs/stm32-chibios-adc-sampling
 * http://www.chibios.org/dokuwiki/doku.php?id=chibios:howtos:createthread
 * http://www.playembedded.org/index.php?lang=en&mod=tut&sec=chi&id=003
 * https://blog.udemy.com/embedded-c-tutorial/
 * http://forum.chibios.org/phpbb/viewtopic.php?f=3&t=1373
 * http://www.scriptoriumdesigns.com/embedded/interrupts.php
 * 
 * Less usefull links
 * http://xmodulo.com/good-ide-for-c-cpp-linux.html
 * 
*/

#endregion


