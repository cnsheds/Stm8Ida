# Stm8Ida
STM8 IDA processor module

Support for STMicroelectronics' STM8 series of microcontrollers

## Linux Building instructions

1. Get the IDA SDK from https://www.hex-rays.com/products/ida/support/download/
2. Move this directory to `$SDKDIR/modules/`. 
3. Run `make`
4. Run `sudo make install`
5. The processor type `SGS-Thomson STM8` should now show up in IDA.
