# Amiga PiStorm32Lite IC2 LCD Display

When I bought my A1200net translucent Amiga 1200 case, and I saw the mounting point for a 1602LCD display, my goal was to use it for my Gotek drive. However in the edit I decided on a combination LCD and rotary switch in the end.

But I still wanted to make use of that mount point, so when I stumbled across information that the PiStorm had I2C output, I thought that would be a great use. Claude from the PIstorm#emu68-dev Discord group pointed in the direction of using the Raspberry Pi camera serial interface (CSI) port, which had I2C output.

## What you will need
- **Hardware**
  - PiStorm32Lite + Raspberry Pi 3A+ (see key things to note below 2.1 - 2.3)
  - 1x Adafruit QT 3V to 5V Level Booster Breakout - https://www.adafruit.com/product/5649?srsltid=AU7gw4VB6NmWp_WYTHhk4kB-RSd3uPEljIShO65xmADcnaBZRQZLk4ij
  - 1x 15 pin 1.0mm pitch FCP Breakout Board - https://www.aliexpress.com/item/1005007617729176.html
  - 1x 10cm (100mm) STEMMA QT / Qwiic JST SH 4-Pin Cable - https://www.adafruit.com/product/4399 (see key things to note below 3)
  - 1x 10cm (100mm) FPC cable - https://www.aliexpress.com/item/1005012135666927.html
  - 1x 4 core cable with dupont connectors (long enough to reach from your PiStorm to the LCD display in the top cover of the case when open. 
- **Software**
  - EMU68 (I ran EMU68 on my PiStorm)

## Key Things to Note

### 1 - Raspberry Pi Model
My PiStorm is connected to a Raspberry Pi 3A+, meaning I haven't tried it with anything else.

### 2 - CSI Port Pinout / Breakout board
I don't know why, maybe its because the FPC cable orientation. But the CSI pinout, did not match the FPC breakout board i.e. 1 did not equal 1. In the end the numbering on the breakout board had to be flipped to work.

#### 2.1 - Rasperberry Pi Camera Serial Interface Pinout
As you can see in the table below, the IC2 pins are 10, 13, 14 and 15.

| Pin #       | Name        | Description |
| ----------- | ----------- | ----------- |
| 10          | GND         | Ground      |
| 13          | CAM_SCL     | I2C SCL     |
| 14          | CAM_SDA     | I2C SDA     |
| 15          | CAM_3V3     | 3.3V Power  |

#### 2.2 - Stemma Cable Pinout
Here is the I2C pinout for the stemma cable that will be used. This is aligned to the pinout of the Adafruit level shifter (more about that later).
![Alt Text](images/stemma_cable.png)

#### 2.3 - FPC Breakout Board Pinout
And this shows how the stemma cable was connected to the FPC breakout board in order for this to work. If you note the colours of the stemma cable and the CSI pinout, you'll see that it doesn't line up with the numbering on the breakout board. Why? I don't know, but this is how to connect it to work.
![Alt Text](images/pistorn32Lite_I2CLCD_breakout+levelshifter_connected.jpg)

#### 3 - Stemma cables
When I did this I bought 2x stemma cables, however you only need the connector at one end of each cable, to plug into the level-shifter. So the wisdom I wish to pass on is just buy 1 10cm cable and cut it in half. One will be soldered to the breakout board and connect to the 3.3V side of the level-shifter and the other half will connect to the 5V side of the level shifter and then be soldered to a 4 core cable with dupont connectors which will connect to your LCD display.
