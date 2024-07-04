# Introduction

To test the code we are going to use the MIMXRT685-EVK board connected to the Jetson AGX Orin and to a PC host both running Linux.

# Hardware setup
## MIMXRT685-EVK

### Jumpers
- **JP12** jumper must be set:
  - **2-3** when connected to the Jetson 40-pin header
  - **1-2** when connected to coaster

For our use-case we need that to be in position **2-3**.

- **JP4** must be closed to be able to use the FC0 UART

### I2S pinout
For the I2S part we are going to use **FLEXCOMM4** and **FLEXCOMM6** for the TX, and **FLEXCOMM5** and **FLEXCOMM7** for RX.

Several signals are shared internally so we are only going to interface externally with **FLEXCOMM4** on the header **J27** and with **FLEXCOMM5** on the header **J28**.

In particular:
- **J28.7** - **GND**
- **J28.6** - **CLK** [ *PIO1_3/FC5_SCK* ]
- **J28.5** - **FSYNC** [ *PIO1_4/FC5_TXD_SCL_MISO_WS* ]
- **J28.4** - **SDIN** [ *PIO1_5/FC5_RXD_SDA_MOSI_DATA* ]
- **J27.1** - **SDOUT** [ *PIO0_30/FC4_RXD_SDA_MOSI_DATA* ]

These signals are going to be connected to the Jetson AGX Orin 40-pins header.

### UART
Connect **TX**, **RX** and **GND** on the **J16** header to (for example) an FTDI receiver to have access to the **UART**.

### USB and POWER
Two USB cables are going to be connected to **J5** and **J7** connectors.

## Jetson AGX Orin
From the 40-pins header we need access to:

- PIN6 - **GND**
- PIN12 - **I2S_CLK**
- PIN35 - **I2S_FS**
- PIN38 - **I2S_SDIN**
- PIN40 - **I2S_SDOUT**

## WIRING

### I2S
For I2S we are going to wire the **J27** and **J28** headers on the EVK to the 40-pin header on the Jetson Orin, as follows:

- **J28.7** -> **GND**
- **J28.6** -> **I2S_CLK**
- **J28.5** -> **I2S_FS**
- **J28.4** -> **I2S_SDOUT**
- **J27.1** -> **I2S_SDIN**

### UART
Using a UART-USB cable, connect the **J16** header to a USB port on your PC

### USB and power
Connect **J5** and **J7** to the PC used for programming the board and acting as USB host.

# Software setup
## PC host

### Development environment
For the complete documentation please refer to the [vscode-for-mcux wiki](https://github.com/nxp-mcuxpresso/vscode-for-mcux/wiki).

Step-by-step procedure:
- Download Visual Studio Code for your host / architecture.

- From the extensions marketplace download the `MCUXpresso for VS Code` extension:
<img width="470" alt="Screenshot 2024-07-04 at 11 18 28" src="https://github.com/carlocaione/tdm2usb/assets/357758/55ce1740-fae8-4b79-a5df-277a1e5246a0">

- From the `QUICKSTART PANEL` select `Open MCUXpresso Installer`:
<img width="470" alt="Screenshot 2024-07-04 at 11 19 22" src="https://github.com/carlocaione/tdm2usb/assets/357758/8a415b4a-a68d-4af3-a71b-076f863f447b">

- From the installer window select everything but the `Zephyr Developer` entry and start the installation:
<img width="651" alt="Screenshot 2024-07-04 at 11 23 45" src="https://github.com/carlocaione/tdm2usb/assets/357758/ec66e845-d9ce-4fec-b6c5-04bd637d21de">

- In the `INSTALLED REPOSITORIES` window, click on `Import Repository`, fill the entries as needed and click the `Import` button:
<img width="1109" alt="Screenshot 2024-07-04 at 11 29 07" src="https://github.com/carlocaione/tdm2usb/assets/357758/663a2c14-5d40-4e7f-aa20-1c0593af117e">

- Clone the `tdm2usb` repo somewhere accessible.

- In the `PROJECTS` window, click on `Import Project`, then select the `Folder` containing the project, fill the remaining entries as suggested by the extension and click on `Import`:
<img width="1109" alt="Screenshot 2024-07-04 at 11 35 43" src="https://github.com/carlocaione/tdm2usb/assets/357758/8f50be4f-5008-43bc-a33a-43318f052af9">

- To compile the project, right-click on the project name in the `PROJECTS` window and select `Pristine Build/Rebuild Selected`:
<img width="421" alt="Screenshot 2024-07-04 at 11 38 09" src="https://github.com/carlocaione/tdm2usb/assets/357758/7da777ff-9459-4139-8a0a-2fcb15e8b49e">

- To flash the code on the EVK, connect the board and click on the `Debug` button besides the project name in the `PROJECTS` window:
<img width="361" alt="Screenshot 2024-07-04 at 11 41 04" src="https://github.com/carlocaione/tdm2usb/assets/357758/da32793d-f0cd-4d1a-892b-9631b1b2b5a2">

> [!IMPORTANT]  
> There is currently a bug in the extension for which you usually have to flash / debug TWICE (`Debug` -> `Stop` -> `Debug` -> `Run`) to be able to correctly run the code.




### USB debug
Refer to your UART-USB converter documentation about how to access the serial console provided by the EVK (this is usually done using `minicom` or equivalent).

UART setting is 115200 8N1 with no flow control.

## Jetson AGX Orin
On the Jetson AGX Orin, the I2S controller is configured using the `amixer` command as follows:

```bash
amixer -c APE cset name="I2S2 Mux" ADMAIF2
amixer -c APE cset name="ADMAIF2 Mux" I2S2
amixer -c APE cset name="I2S2 codec master mode" cbs-cfs
amixer -c APE cset name="I2S2 codec frame mode" dsp-a
amixer -c APE cset name="I2S2 Capture Audio Bit Format" 32
amixer -c APE cset name="I2S2 Playback Audio Bit Format" 32
amixer -c APE cset name="I2S2 Client Bit Format" 32
amixer -c APE cset name="I2S2 Client Channels" 16

amixer -c APE cset name="I2S2 Capture Audio Channels" 16
amixer -c APE cset name="I2S2 Playback Audio Channels" 16
amixer -c APE cset name="I2S2 FSYNC Width" 0
amixer -c APE cset name="I2S2 Sample Rate" 48000

amixer -c APE cset name="I2S2 Loopback" off
amixer -c APE cset name="I2S2 Capture Data Offset" 2
```

# HOWTO
To test both the communication streams (**IN** and **OUT**) we use `arecord` to record the data stream, and `speaker-test` to generate the audio stream, both running on the Jetson ORIN and/or the host PC.

## IN
We are testing the following configuration:
```
               <- EP 2 IN (data) [SOURCE]
              +--------------+
         USB  |              |  I2S
  PC  <-------+      IN      <-------+ Jetson AGX Orin
              |              |  RX
              +--------------+
                  RT685 EVK
```
with the PC recording over USB the data generated by the Jetson and sent over I2S. In this case we only have on single EP IN that is using implicit feedback to modulate the stream rate.

This configuration can be tested by doing:
```bash
[PC]     arecord -D hw:TDM2USB,0 -c 16 pc_record.wav -r 48000 -f S32_LE -d 60 -t wav
[Jetson] speaker-test -c 16 -f 1000 -F S32_LE -D hw:APE,1 -t sine
```
We are doing a 60s WAV recording on the PC and gathering the 1kHz sine wave per channel generated on the Jetson.  The data generated and recorded is: 48kHz / 16 channels / 32 bits per channel.

## OUT
We are testing the following configuration:
```
               <- EP 1 IN (feedback)
               -> EP 1 OUT (data) [SINK]
              +--------------+
         USB  |              |  I2S
  PC  +------->     OUT      +-------> Jetson AGX Orin
              |              |  TX
              +--------------+
                  RT685 EVK
```
with the Jetson recording over I2S the data generated by the PC and sent over USB. In this case we have an explicit feedback IN endpoint that is modulating the stream rate.

This configuration can be tested by doing:
```
[Jetson] arecord -D hw:APE,1 -c 16 jetson_record.wav -r 48000 -f S32_LE -d 60 -t wav
[PC]     speaker-test -c 16 -f 1000 -F S32_LE -D hw:TDM2USB,0 -t sine
```

The PC is generating the 1kHz sine wave per channel data sent through USB to the Jetson that is gathering data recording from the I2S interface. The data generated and recorded is: 48kHz / 16 channels / 32 bits per channel.

## IN + OUT
There are two easy ways to test both the streams at the same time:

1. we do what was described in the previous sections at the same time on the PC and on the Jetson
2. we use `alsaloop` to create a loop

### Using `alsaloop`

`alsaloop` is (unsurprisingly) creating a loop using ALSA, so we can verify that we can send and receive data at the same time.

If we want to have the loopback on the USB (PC) side so that we can send and record from the I2S (Jetson) side, we can do that by doing in sequence:
```
[Jetson] speaker-test -c 16 -f 1000 -F S32_LE -D hw:APE,1 -t sine
[PC]     alsaloop -P hw:TDM2USB,0 -C hw:TDM2USB,0 -c 16 -f S32_LE -r 48000 -v
[Jetson] arecord -D hw:APE,1 -c 16 jetson_recorded.wav -r 48000 -f S32_LE -d 10 -t wav
```

In this case we are instructing the PC to send over the playback USB interface, whatever we receive from the capture USB interface. We then send data through I2S on the Jetson side, and we record a 10 seconds wave file from the Jetson itself with whatever we receive from the same interface.

The same can be done by looping the I2S side instead of the USB side:
```
[PC]     speaker-test -c 16 -f 1000 -F S32_LE -D hw:TDM2USB,0 -t sine
[Jetson] alsaloop -P hw:APE,1 -C hw:APE,1 -c 16 -f S32_LE -r 48000 -v
[PC]     arecord -D hw:TDM2USB,0 -c 16 pc_recorded.wav -r 48000 -f S32_LE -d 10 -t wav
```
The scenario is the opposite of what we previously described, by looping this time the I2S playback / capture interface and streaming data and recording on the USB side.
