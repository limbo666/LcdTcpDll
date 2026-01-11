# LCD TCP Display Driver for LCD Smartie (v1.0.0.2b)

This is a high-performance, multithreaded Windows DLL designed as a display plugin for **LCD Smartie**. It allows the software to communicate with remote LCD displays over a network (WiFi/Ethernet) via the TCP protocol.

### Credits

This project is a heavily enhanced fork of the original work by **eeyrw** ([GitHub Profile](https://github.com/eeyrw)). This version has been modernized to support expanded hardware features and improved network stability.

----------

##  Key Features in v1.0.0.2b

-   **Multithreaded Architecture:** The TCP communication now runs on a separate thread, preventing LCD Smartie from "freezing" or lagging during network congestion.    
-   **ESP8266 Stability Fix:** Disabled `TCP_NODELAY` to utilize Nagle’s Algorithm, which prevents overwhelming the ESP8266 CPU and reduces watchdog timer (WDT) resets.    
-   **GPO & Fan Control:** Full support for standard Matrix Orbital commands to trigger physical pins on the remote device.    
-   **Modern Build System:** Native support for **Visual Studio 2026** (x64 configuration).    

----------

## Building the Driver

Unlike the original version which used CodeBlocks/MinGW, this version is optimized for **Visual Studio**:

1.  Open the `.sln` solution file in Visual Studio 2022.    
2.  Select your platform (**x64** is recommended for modern systems).    
3.  Set the configuration to **Release**.    
4.  Click **Build Solution**.    
5.  The final DLL will be located in the `x64\Release` or `Release` folder.
    

----------

## How to Use

### 1. Installation
Copy `LCD_TCP_DLL.dll` from the build output to your `LCD_SMARTIE_ROOT\displays` folder.

### 2. Configuration
1.  Open **LCD Smartie**.    
2.  Go to **Setup** > **Display Settings**.    
3.  Select `LCD_TCP_DLL.dll` from the drop-down menu.    
4.  Enter the **IP Address** and **Port** (default 2400) shown on your LCD screen (e.g., `192.168.1.50:2400`).    


----------

## Commands for GPO and Fans
#### Examples 
Button press to go to next screen

    Action01Variable=$MObutton(1)
    Action01Condition=2
    Action01ConditionValue=1
    Action01Action=NextScreen
    Action01Enabled=True

Button press to go to theme 2

    Action01Variable=$MObutton(2)
    Action01Condition=2
    Action01ConditionValue=1
    Action01Action=GotoTheme(2)
    Action01Enabled=True

Button press to go to flash backlight
 
    Action01Variable=$MObutton(2)
    Action01Condition=2
    Action01ConditionValue=1
    Action01Action=BacklightFlash(2)
    Action01Enabled=True

Button to toggle output

     Action01Variable=$MObutton(2) 
     Action01Condition=2
     Action01ConditionValue=1 Action01Action=GPOToggle(1)
     Action01Enabled=True

Set fan to full on screen 5

    Action01Variable=$ActiveScreen
    Action01Condition=2
    Action01ConditionValue=5
    Action01Action=Fan(1,255)
    Action01Enabled=True








----------

### License

This project is licensed under the **MIT License**, consistent with the original author's update.
