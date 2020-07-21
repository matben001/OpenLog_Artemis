//Power down the entire system but maintain running of RTC
//This function takes 100us to run including GPIO setting
//This puts the Apollo3 into 2.36uA to 2.6uA consumption mode
//With leakage across the 3.3V protection diode, it's approx 3.00uA.
void powerDown()
{ 
  //Prevent voltage supervisor from waking us from sleep
  detachInterrupt(digitalPinToInterrupt(PIN_POWER_LOSS));

  //Prevent stop logging button from waking us from sleep
  if (settings.useGPIO32ForStopLogging == true)
  {
    detachInterrupt(digitalPinToInterrupt(PIN_STOP_LOGGING)); // Disable the interrupt
    pinMode(PIN_STOP_LOGGING, INPUT); // Remove the pull-up
  }

  //WE NEED TO POWER DOWN ASAP - we don't have time to close the SD files
  //Save files before going to sleep
  //  if (online.dataLogging == true)
  //  {
  //    sensorDataFile.sync();
  //    sensorDataFile.close();
  //  }
  //  if (online.serialLogging == true)
  //  {
  //    serialDataFile.sync();
  //    serialDataFile.close();
  //  }

  //Serial.flush(); //Don't waste time waiting for prints to finish

  //  Wire.end(); //Power down I2C
  qwiic.end(); //Power down I2C

  SPI.end(); //Power down SPI

  power_adc_disable(); //Power down ADC. It it started by default before setup().

  Serial.end(); //Power down UART
  SerialLog.end();

  //Force the peripherals off
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM1);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM2);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM3);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM4);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM5);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_ADC);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART1);

  //Disable pads
  for (int x = 0; x < 50; x++)
  {
    if ((x != ap3_gpio_pin2pad(PIN_POWER_LOSS)) &&
      //(x != ap3_gpio_pin2pad(PIN_LOGIC_DEBUG)) &&
      (x != ap3_gpio_pin2pad(PIN_MICROSD_POWER)) &&
      (x != ap3_gpio_pin2pad(PIN_QWIIC_POWER)) &&
      (x != ap3_gpio_pin2pad(PIN_IMU_POWER)))
    {
      am_hal_gpio_pinconfig(x, g_AM_HAL_GPIO_DISABLE);
    }
  }

  //powerLEDOff();
  
  //Make sure PIN_POWER_LOSS is configured as an input for the WDT
  pinMode(PIN_POWER_LOSS, INPUT); // BD49K30G-TL has CMOS output and does not need a pull-up

  //We can't leave these power control pins floating
  imuPowerOff();
  microSDPowerOff();

  //Keep Qwiic bus powered on if user desires it - but only for X04 to avoid a brown-out
#if(HARDWARE_VERSION_MAJOR == 0)
  if (settings.powerDownQwiicBusBetweenReads == true)
    qwiicPowerOff();
  else
    qwiicPowerOn(); //Make sure pins stays as output
#else
  qwiicPowerOff();
#endif

  //Power down Flash, SRAM, cache
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_CACHE);         //Turn off CACHE
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_FLASH_512K);    //Turn off everything but lower 512k
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_SRAM_64K_DTCM); //Turn off everything but lower 64k
  //am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_ALL); //Turn off all memory (doesn't recover)

  //Keep the 32kHz clock running for RTC
  am_hal_stimer_config(AM_HAL_STIMER_CFG_CLEAR | AM_HAL_STIMER_CFG_FREEZE);
  am_hal_stimer_config(AM_HAL_STIMER_XTAL_32KHZ);

  while (1) // Stay in deep sleep until we get reset
  {
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP); //Sleep
  }
}

//Power everything down and wait for interrupt wakeup
void goToSleep()
{
  //Counter/Timer 6 will use the 32kHz clock
  //Calculate how many 32768Hz system ticks we need to sleep for:
  //sysTicksToSleep = msToSleep * 32768L / 1000
  //We need to be careful with the multiply as we will overflow uint32_t if msToSleep is > 131072
  uint32_t msToSleep = (uint32_t)(settings.usBetweenReadings / 1000ULL);
  uint32_t sysTicksToSleep;
  if (msToSleep < 131000)
  {
    sysTicksToSleep = msToSleep * 32768L; // Do the multiply first for short intervals
    sysTicksToSleep = sysTicksToSleep / 1000L; // Now do the divide
  }
  else
  {
    sysTicksToSleep = msToSleep / 1000L; // Do the division first for long intervals (to avoid an overflow)
    sysTicksToSleep = sysTicksToSleep * 32768L; // Now do the multiply
  }
  
  //Prevent voltage supervisor from waking us from sleep
  detachInterrupt(digitalPinToInterrupt(PIN_POWER_LOSS));

  //Prevent stop logging button from waking us from sleep
  if (settings.useGPIO32ForStopLogging == true)
  {
    detachInterrupt(digitalPinToInterrupt(PIN_STOP_LOGGING)); // Disable the interrupt
    pinMode(PIN_STOP_LOGGING, INPUT); // Remove the pull-up
  }
  
  //Save files before going to sleep
  if (online.dataLogging == true)
  {
    sensorDataFile.sync();
    sensorDataFile.close(); //No need to close files. https://forum.arduino.cc/index.php?topic=149504.msg1125098#msg1125098
  }
  if (online.serialLogging == true)
  {
    serialDataFile.sync();
    serialDataFile.close();
  }

  delay(sdPowerDownDelay); // Give the SD card time to finish writing ***** THIS IS CRITICAL *****

  Serial.flush(); //Finish any prints

  //  Wire.end(); //Power down I2C
  qwiic.end(); //Power down I2C

  SPI.end(); //Power down SPI

  power_adc_disable(); //Power down ADC. It it started by default before setup().

  Serial.end(); //Power down UART
  SerialLog.end();

  //Force the peripherals off
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM1);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM2);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM3);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM4);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_IOM5);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_ADC);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART0);
  am_hal_pwrctrl_periph_disable(AM_HAL_PWRCTRL_PERIPH_UART1);

  //Disable pads
  for (int x = 0; x < 50; x++)
  {
    if ((x != ap3_gpio_pin2pad(PIN_POWER_LOSS)) &&
      //(x != ap3_gpio_pin2pad(PIN_LOGIC_DEBUG)) &&
      (x != ap3_gpio_pin2pad(PIN_MICROSD_POWER)) &&
      (x != ap3_gpio_pin2pad(PIN_QWIIC_POWER)) &&
      (x != ap3_gpio_pin2pad(PIN_IMU_POWER)))
    {
      am_hal_gpio_pinconfig(x, g_AM_HAL_GPIO_DISABLE);
    }
  }

  //Make sure PIN_POWER_LOSS is configured as an input for the WDT
  pinMode(PIN_POWER_LOSS, INPUT); // BD49K30G-TL has CMOS output and does not need a pull-up

  //We can't leave these power control pins floating
  imuPowerOff();
#if((HARDWARE_VERSION_MAJOR == 0) && (HARDWARE_VERSION_MINOR == 5))
  // For high speed logging tests on x04:
  microSDPowerOff();
#else  
  microSDPowerOff();
#endif

  //Keep Qwiic bus powered on if user desires it
  if (settings.powerDownQwiicBusBetweenReads == true)
    qwiicPowerOff();
  else
    qwiicPowerOn(); //Make sure pins stays as output

  //Leave the power LED on if the user desires it
  if (settings.enablePwrLedDuringSleep == true)
    powerLEDOn();
  //else
  //  powerLEDOff();

  //Power down Flash, SRAM, cache
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_CACHE);         //Turn off CACHE
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_FLASH_512K);    //Turn off everything but lower 512k
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_SRAM_64K_DTCM); //Turn off everything but lower 64k
  //am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_ALL); //Turn off all memory (doesn't recover)

  //Use the lower power 32kHz clock. Use it to run CT6 as well.
  am_hal_stimer_config(AM_HAL_STIMER_CFG_CLEAR | AM_HAL_STIMER_CFG_FREEZE);
  am_hal_stimer_config(AM_HAL_STIMER_XTAL_32KHZ | AM_HAL_STIMER_CFG_COMPARE_G_ENABLE);

  //Adjust sysTicks down by the amount we've be at 48MHz
  uint32_t msBeenAwake = millis();
  uint32_t sysTicksAwake = msBeenAwake * 32768L / 1000L; //Convert to 32kHz systicks

#if((HARDWARE_VERSION_MAJOR == 0) && (HARDWARE_VERSION_MINOR == 5))
  // For high speed logging tests on x04, always sleep for the full sysTicksToSleep
  sysTicksToSleep += sysTicksAwake;
#else
  //Check that sysTicksToSleep is >> sysTicksAwake
  if (sysTicksToSleep > (sysTicksAwake + 3277)) // Abort if we are trying to sleep for < 100ms
#endif
  {
    sysTicksToSleep -= sysTicksAwake;
  
    //Setup interrupt to trigger when the number of ms have elapsed
    am_hal_stimer_compare_delta_set(6, sysTicksToSleep);
  
    //We use counter/timer 6 to cause us to wake up from sleep but 0 to 7 are available
    //CT 7 is used for Software Serial. All CTs are used for Servo.
    am_hal_stimer_int_clear(AM_HAL_STIMER_INT_COMPAREG);  //Clear CT6
    am_hal_stimer_int_enable(AM_HAL_STIMER_INT_COMPAREG); //Enable C/T G=6
  
    //Enable the timer interrupt in the NVIC.
    NVIC_EnableIRQ(STIMER_CMPR6_IRQn);
  
    //Halt the WDT otherwise this will bring us out of deep sleep
    am_hal_wdt_halt();
  
    //Deep Sleep
    am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
  
    //(Re)start the WDT
    am_hal_wdt_start();
  
    //Turn off interrupt
    NVIC_DisableIRQ(STIMER_CMPR6_IRQn);
    am_hal_stimer_int_disable(AM_HAL_STIMER_INT_COMPAREG); //Disable C/T G=6
  }

  //We're BACK!
  wakeFromSleep();
}

//Power everything up gracefully
void wakeFromSleep()
{
  //Power up SRAM, turn on entire Flash
  am_hal_pwrctrl_memory_deepsleep_powerdown(AM_HAL_PWRCTRL_MEM_MAX);

  //Go back to using the main clock
  //am_hal_stimer_int_enable(AM_HAL_STIMER_INT_OVERFLOW);
  //NVIC_EnableIRQ(STIMER_IRQn);
  am_hal_stimer_config(AM_HAL_STIMER_CFG_CLEAR | AM_HAL_STIMER_CFG_FREEZE);
  am_hal_stimer_config(AM_HAL_STIMER_HFRC_3MHZ);

  //Turn on ADC
  ap3_adc_setup();

  //Run setup again

  //If 3.3V rail drops below 3V, system will enter low power mode and maintain RTC
  pinMode(PIN_POWER_LOSS, INPUT); // BD49K30G-TL has CMOS output and does not need a pull-up

  delay(1); // Let PIN_POWER_LOSS stabilize

  attachInterrupt(digitalPinToInterrupt(PIN_POWER_LOSS), powerDown, FALLING);

  if (digitalRead(PIN_POWER_LOSS) == LOW) powerDown(); //Check PIN_POWER_LOSS just in case we missed the falling edge

  if (settings.useGPIO32ForStopLogging == true)
  {
    pinMode(PIN_STOP_LOGGING, INPUT_PULLUP);
    delay(1); // Let the pin stabilize
    attachInterrupt(digitalPinToInterrupt(PIN_STOP_LOGGING), stopLoggingISR, FALLING); // Enable the interrupt
    stopLoggingSeen = false; // Make sure the flag is clear
  }

  pinMode(PIN_STAT_LED, OUTPUT);
  digitalWrite(PIN_STAT_LED, LOW);

  powerLEDOn();

  Serial.begin(settings.serialTerminalBaudRate);

  SPI.begin(); //Needed if SD is disabled

  beginSD(); //285 - 293ms

  //Add CIPO pull-up
  ap3_err_t retval = AP3_OK;
  am_hal_gpio_pincfg_t cipoPinCfg = AP3_GPIO_DEFAULT_PINCFG;
  cipoPinCfg.uFuncSel = AM_HAL_PIN_6_M0MISO;
  cipoPinCfg.eDriveStrength = AM_HAL_GPIO_PIN_DRIVESTRENGTH_12MA;
  cipoPinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_PUSHPULL;
  cipoPinCfg.uIOMnum = AP3_SPI_IOM;
  cipoPinCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_1_5K;
  padMode(MISO, cipoPinCfg, &retval);
  if (retval != AP3_OK)
    printDebug("Setting CIPO padMode failed!");
    
  beginQwiic(); //Power up Qwiic bus
  long powerStartTime = millis();

  beginDataLogging(); //180ms

  beginSerialLogging(); //20 - 99ms

  beginIMU(); //61ms

  //If we powered down the Qwiic bus, then re-begin and re-configure everything
  if (settings.powerDownQwiicBusBetweenReads == true)
  {
    //Before we talk to Qwiic devices we need to allow the power rail to settle
    while (millis() - powerStartTime < settings.qwiicBusPowerUpDelayMs) //Testing, 100 too short, 200 is ok
    {
      delay(1); //Wait
    }

    beginQwiicDevices();
    //loadDeviceSettingsFromFile(); //Apply device settings after the Qwiic bus devices have been detected and begin()'d
    configureQwiicDevices(); //Apply config settings to each device in the node list
  }

  //Serial.printf("Wake up time: %.02f ms\n", (micros() - startTime) / 1000.0);

  //When we wake up micros has been reset to zero so we need to let the main loop know to take a reading
  takeReading = true;
}

void stopLogging(void)
{
  detachInterrupt(digitalPinToInterrupt(PIN_STOP_LOGGING)); // Disable the interrupt
  
  //Save files before going to sleep
  if (online.dataLogging == true)
  {
    sensorDataFile.sync();
    sensorDataFile.close(); //No need to close files. https://forum.arduino.cc/index.php?topic=149504.msg1125098#msg1125098
  }
  if (online.serialLogging == true)
  {
    serialDataFile.sync();
    serialDataFile.close();
  }
  
  Serial.print("Logging is stopped. Please reset OpenLog Artemis and open a terminal at ");
  Serial.print((String)settings.serialTerminalBaudRate);
  Serial.println("bps...");
  delay(sdPowerDownDelay); // Give the SD card time to shut down
  powerDown();
}

void qwiicPowerOn()
{
  pinMode(PIN_QWIIC_POWER, OUTPUT);
#if(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 4)
  digitalWrite(PIN_QWIIC_POWER, LOW);
#elif(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 5)
  digitalWrite(PIN_QWIIC_POWER, LOW);
#elif(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 6)
  digitalWrite(PIN_QWIIC_POWER, HIGH);
#elif(HARDWARE_VERSION_MAJOR == 1 && HARDWARE_VERSION_MINOR == 0)
  digitalWrite(PIN_QWIIC_POWER, HIGH);
#endif
}
void qwiicPowerOff()
{
  pinMode(PIN_QWIIC_POWER, OUTPUT);
#if(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 4)
  digitalWrite(PIN_QWIIC_POWER, HIGH);
#elif(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 5)
  digitalWrite(PIN_QWIIC_POWER, HIGH);
#elif(HARDWARE_VERSION_MAJOR == 0 && HARDWARE_VERSION_MINOR == 6)
  digitalWrite(PIN_QWIIC_POWER, LOW);
#elif(HARDWARE_VERSION_MAJOR == 1 && HARDWARE_VERSION_MINOR == 0)
  digitalWrite(PIN_QWIIC_POWER, LOW);
#endif
}

void microSDPowerOn()
{
  pinMode(PIN_MICROSD_POWER, OUTPUT);
  digitalWrite(PIN_MICROSD_POWER, LOW);
}
void microSDPowerOff()
{
  pinMode(PIN_MICROSD_POWER, OUTPUT);
  digitalWrite(PIN_MICROSD_POWER, HIGH);
}

void imuPowerOn()
{
  pinMode(PIN_IMU_POWER, OUTPUT);
  digitalWrite(PIN_IMU_POWER, HIGH);
}
void imuPowerOff()
{
  pinMode(PIN_IMU_POWER, OUTPUT);
  digitalWrite(PIN_IMU_POWER, LOW);
}

void powerLEDOn()
{
#if(HARDWARE_VERSION_MAJOR >= 1)
  pinMode(PIN_PWR_LED, OUTPUT);
  digitalWrite(PIN_PWR_LED, HIGH); // Turn the Power LED on  
#endif  
}
void powerLEDOff()
{
#if(HARDWARE_VERSION_MAJOR >= 1)
  pinMode(PIN_PWR_LED, OUTPUT);
  digitalWrite(PIN_PWR_LED, LOW); // Turn the Power LED off
#endif  
}

//Returns the number of milliseconds according to the RTC
//(In increments of 10ms)
//Watch out for the year roll-over!
uint64_t rtcMillis()
{
  myRTC.getTime();
  uint64_t millisToday = 0;
  int dayOfYear = calculateDayOfYear(myRTC.dayOfMonth, myRTC.month, myRTC.year + 2000);
  millisToday += ((uint64_t)dayOfYear * 86400000ULL);
  millisToday += ((uint64_t)myRTC.hour * 3600000ULL);
  millisToday += ((uint64_t)myRTC.minute * 60000ULL);
  millisToday += ((uint64_t)myRTC.seconds * 1000ULL);
  millisToday += ((uint64_t)myRTC.hundredths * 10ULL);

  return (millisToday);
}

//Returns the day of year
//https://gist.github.com/jrleeman/3b7c10712112e49d8607
int calculateDayOfYear(int day, int month, int year)
{  
  // Given a day, month, and year (4 digit), returns 
  // the day of year. Errors return 999.
  
  int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  
  // Verify we got a 4-digit year
  if (year < 1000) {
    return 999;
  }
  
  // Check if it is a leap year, this is confusing business
  // See: https://support.microsoft.com/en-us/kb/214019
  if (year%4  == 0) {
    if (year%100 != 0) {
      daysInMonth[1] = 29;
    }
    else {
      if (year%400 == 0) {
        daysInMonth[1] = 29;
      }
    }
   }

  // Make sure we are on a valid day of the month
  if (day < 1) 
  {
    return 999;
  } else if (day > daysInMonth[month-1]) {
    return 999;
  }
  
  int doy = 0;
  for (int i = 0; i < month - 1; i++) {
    doy += daysInMonth[i];
  }
  
  doy += day;
  return doy;
}