# Amiga PiStorm32Lite IC2 LCD Display

When I bought my A1200net translucent Amiga 1200 case, and I saw the mounting point for a 1602LCD display, my goal was to use it for my Gotek drive. However in the edit I decided on a combination LCD and rotary switch in the end.

But I still wanted to make use of that mount point, so when I stumbled across information that the PiStorm had I2C output, I thought that would be a great use. Claude from the PIstorm#emu68-dev Discord group pointed in the direction of using the Raspberry Pi camera serial interface (CSI) port, which had I2C output.

## Key Things to Note

### 1 - Raspberry Pi Model
My PiStorm is connected to a Raspberry Pi 3A+

### 2 - CSI Port Pinout / Breakout board



| Pin #       | Name        | Description |
| ----------- | ----------- | ----------- |
| 10          | GND         | Ground      |
| 13          | CAM_SCL     | I2C SCL     |
| 14          | CAM_SDA     | I2C SDA     |
| 15          | CAM_3V3     | 3.3V Power  |

![Alt Text](images/pistorn32Lite_I2CLCD_breakout+levelshifter_connected.jpg)
