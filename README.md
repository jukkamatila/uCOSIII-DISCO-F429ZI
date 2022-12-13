# µC/OS-III STM32F4 Port

This is a third year Vaasa Univercity of Applied Science optional project from course IITS2106-3004 Real Time Operating Systems. The purpose was to study uC/OS-III on STM32F429I-Discovery MCU breakboard.
Original author was Gao Boli who graduated and left the project. Lecturer Jukka Matila forked the project 3 years ago. And I (Siyuan Xu) was authorized to maintain the project on 12.2022.
The objectives are:
1. Update the repository up to date for Build and Upload
2. Optimize the code for better performace
3. Create a snake game (just for fun, not a promise)
4. Integrate all apps together (just for fun, not a promise)
    * Collect all apps into one project
    * Create a simple GUI menu for different APPs 

Siyuan Xu, e2101066
Vaasa, 2022

# Instruction for programming stm32F429I-Discovery with PlatformIO

## Step 0 - Non't follow the Instruction "ucos_boli_stm32_instruction.docx"!

That instruction doesn't work anymore. Perhaps because it was made more than two years ago, there are changes in PlatformIO.

## Step 1 - Install the tool-chain

### Install VS-Code and C/C++ Extension Pack, platformio extentions

https://code.visualstudio.com/

### Install STM32CubeIDE

https://www.st.com/en/development-tools/stm32cubeide.html

### Install ST-LINK009

https://www.st.com/en/development-tools/stsw-link009.html

## Step 2 - clone the repository

git clone https://github.com/jukkamatila/uCOSIII-DISCO-F429ZI.git
Or download the code as zip

## Step 3 - Open the target directory with VS-code

You should open the directory under *UCOSIII-DISCO-F429ZI*, **NOT** the *UCOSIII-DISCO-F429ZI* itself. For example open *UCOSIII-DISCO-F429ZI/Tic-Tac-Toe*

## Step 4 - Modify the platformio.ini file

``` t
[env:disco_f429zi]
platform = ststm32
board = disco_f429zi
board_build.stm32cube.variant = STM32F429I-Discovery    # Manually adding the driver library directory
framework = stm32cube

lib_ldf_mode = deep+

monitor_speed = 115200
```

## Step 5 - Modify the library file

C:\Users\\*USERNAME*\\.platformio\packages\framework-stm32cubef4\Drivers\BSP\STM32F429I-Discovery\stm32f429i_discovery_lcd.c
Comment out all the includes below
``` C
//#include "../../../Utilities/Fonts/font24.c"
//#include "../../../Utilities/Fonts/font20.c"
//#include "../../../Utilities/Fonts/font16.c"
//#include "../../../Utilities/Fonts/font12.c"
//#include "../../../Utilities/Fonts/font8.c"
```

## Step 6 - Build and Upload

### Click the Alien icon on the left panel of VS-code, under *disco_f429zi/General/* you can find tools such as Build, Upload, Monitor...

### - Build first

### - Upload

Don't forgot to connect your stm32 board

# Now go nuts and explor yourself!

![PlatformIO](/assets/images/platformio.jpg "PlatformIO")
