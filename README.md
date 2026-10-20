# Split-Flap · ESP32 模組版（10 字元）

本專案從 [JonnyBooker/split-flap](https://github.com/JonnyBooker/split-flap) 的固定版本建立新分支，延續 David Königsmann 的機構設計，控制系統改成 **一片 ESP32＋現成模組＋洞洞板**，作為本專案的標準組建方式。保留原本 10 字元、每字元 45 片翻頁的機構與字元順序，不再要求 ESP-01、10 片 Nano 或訂製 PCB。

> **工程原型，尚未實物驗證。** 尚未完成實板接線、燒錄、馬達／電源／十字元測試。軟體測試不是硬體安全認證。先閱讀電源及組裝門檻，再接線或通電。

## 從這裡開始

1. [實作架構與元件分工](docs/esp32/architecture.md)
2. [逐腳接線、位址、供電與 RESET](docs/esp32/wiring.md)
3. [元件檢查、機構比對與分階段組裝](docs/esp32/assembly.md)
4. [編譯、串列命令、歸零與校準](docs/esp32/firmware.md)
5. [軟體驗證結果與硬體驗收表](docs/esp32/validation.md)
6. [參考版本、相容性比對與功能移植計畫](docs/esp32/upstream-reference.md)
7. [標準組建 BOM](hardware/project-bom.csv) 與[選配零件](hardware/optional-parts.csv)

## 實際採用的架構

- ESP-WROOM-32 30pin Type-C／CH340 主控，PlatformIO 的 `esp32dev` 為待確認實板的起始設定
- 10 顆 **12V** 28BYJ-48、10 片 ULN2003、10 片 KY-003
- **4 片 MCP23017**：`0x20–0x22` 共 40 路馬達相位；`0x23` 集中 10 路 Hall
- **1 片 8 路 MOS 電位轉換板**：SDA、SCL、/RESET 用 3 路；ESP32 為 3.3V，MCP/Hall 為 5V
- **1 片 5V 降壓板**供控制電源；不得並聯降壓板輸出。實際持續電流／溫升待測
- 12V 馬達匯流排＋5V 控制匯流排，共地；12V 電源的極性、連續與峰值電流能力須先驗證；PD/QC 觸發線為選配輸入方式

Hall 全在 5V 側，避開現行 MCP23017 資料表只允許輸出的 GPA7/GPB7，並減少每輪 Hall 讀取到一筆 16-bit I²C 交易。

## 新韌體已實作

`firmware/esp32/` 是此架構的唯一新韌體入口：

- 非阻塞、最多十馬達排程；每輪每馬達最多一步，延遲時不暴衝追趕
- MCP OLAT 影子暫存及每片 16-bit 批次更新，100kHz I²C
- 開機不動作、不自動歸零；明確初始化、逐級啟用、ARM、HOME
- 離開既有磁區再尋找有效邊緣；5ms debounce、步數與時間雙重歸零限制
- 每字元半步／圈、零點偏移、相序方向、步距、Hall 極性校準與 NVS 儲存
- 45 字元原版順序、UTF-8 ÄÖÜ 與替代 `$&#` 翻頁；往前跨圈時重新歸零
- 停止／故障釋放線圈、位置失效；I²C 失敗、配置變動及迴圈停滯鎖定
- 主機端單元測試與 ESP32 交叉編譯；結果見[驗證紀錄](docs/esp32/validation.md)

**本版先用 USB 串列完成可靠調機。** 原 ESP8266 網頁、Wi-Fi、時間顯示尚未移植，倉庫保留的 JonnyBooker ESP8266/Nano 程式仍是另一套硬體架構。接下來應先完成單字元及十字元量測，再在同一片 ESP32 增加通訊介面。

## 快速建置（尚不可直接接上十顆馬達）

```sh
cd firmware/esp32
pio run
# 確認 USB/外接電源隔離及實板後才燒錄：
pio run --target upload
pio device monitor --baud 115200
```

初次開機保持馬達 12V 關閉，先依文件完成檢查。`INIT POWER_OFF` 只是操作者確認，韌體沒有電壓感測器能驗證你真的斷電。

## 實際上游基底與來源沿革

本分支實際 Git 基底為 JonnyBooker 的 [`0c6ef692`](https://github.com/JonnyBooker/split-flap/tree/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d)。該版本仍採 ESP8266＋Nano；本版 ESP32 韌體為獨立控制實作，未宣稱已移植其 Web、排程、倒數、Wi-Fi 設定或 OTA。其作者說明只能在時間允許下提供協助與更新，不能視為持續維護保證。詳細證據與規劃見[參考比對](docs/esp32/upstream-reference.md)。

`esp32-jonnybooker-base` 的祖先包含上述 JonnyBooker commit，再移入原 ESP32 分支的六筆自訂修改。舊 `esp32-purchased-bom`、PR #1 與 `main` 保持不變。GitHub 的 fork parent 標籤仍指向 David Königsmann，與此分支的 Git 祖先是不同概念。


- [歷史 David 版 README](Instructions/README.upstream.md)
- [原始完整組裝 PDF](Instructions/SplitFlapInstructions.pdf)
- Jonny 基底的 `Unit/`、`ESPMaster/`、`EEPROM_Write_Offset/`、`PCB/` 原封保留，**不適用本分支新接線**
- [原作者 3D 檔來源](https://www.printables.com/model/69464-split-flap-display)；本分支繼承上游 `3dModels/Models.zip`，未另下載或重新設計孔位
- JonnyBooker 的 [3D 備份說明](https://github.com/JonnyBooker/split-flap/blob/0c6ef692ac6a5d21a9bc858fa79ecbe816530d8d/3dModels/README.md) 亦指向同一模型來源；列印前確認發布頁授權與版本。
- 舊 `esp32-purchased-bom` 分支基底：`14a22d9f38900aa520feeb70656357b8fd719e45`；新分支基底如上

授權維持 [GPL-3.0](LICENSE)。原專案、照片、組裝設計歸原作者；本分支新增 ESP32 實作與文件保留其來源。外部 3D 檔的使用以原發布頁授權為準。

## 線上正式韌體模擬

[開啟 Wokwi 閉迴路模擬](https://wokwi.com/projects/478374804791943169)；[快速複現、操作與驗證邊界](simulation/wokwi/README.md)。執行本 repo 正式 ESP32 韌體，四個 MCP 位址、四十相輸出及十個轉子模型自動產生 Hall 回授，可操作 HOME／TEXT／MOVE／CAL／SAVE／STOP。上電不自動啟用，須依說明送入指令。模型不驗證電位轉換、12V 電氣安全、扭力或真實翻頁機構；尚未移植的 Wi-Fi／Web／NTP 亦不在此模擬範圍。


---

## JonnyBooker 上游原始說明（ESP8266＋Nano 架構）

以下保留固定基底的完整 README。其 ESP8266、Nano、客製 PCB 與網路功能說明不適用於上方的 ESP32＋MCP23017 韌體；上游徽章也不是本 ESP32 分支的測試結果。

# Split-Flap

[![Build ESP Master Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-esp-master.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-esp-master.yml) [![Build EEPROM Write Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-eeprom-write.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-eeprom-write.yml) [![Build Arduino Unit Sketch](https://github.com/JonnyBooker/split-flap/actions/workflows/build-unit.yml/badge.svg)](https://github.com/JonnyBooker/split-flap/actions/workflows/build-unit.yml)

![Split Flap Display](./Images/Split-Flap.jpg)

---
**NOTE:** I will make my best attempt to provide answers where possible in issues and try to keep things up to date where possible dependency wise, however I do not have the time/capacity to work on this project on a consistent basis. Answers and updates may take time.

My personal Split-Flap has been working since I first published this repository and continues to work to this day happily. Several people have also successfully been in touch with their working creations of this Split-Flap which I am very happy it has worked for :) 

I hope this project continues to work for any whom wish to embark on it but be aware, it is a lot of work to complete.

Thank you everyone whom has contributed, included in the "main" release or not, it is very appreciated. I hope to keep the project going in future.

<3

---

This project has been forked from the brilliant [Split Flap Project](https://github.com/Dave19171/split-flap) by [David Königsmann](https://github.com/Dave19171). None of this would have been possible without the great foundations that have been put in place.

This project has built on the original project to add extra features such as:

- Message Splittng
  - If a message is longer then the number of units there are, the message will be split up and displayed in sequence with a delay between each message
  - Also messages can be split up by adding a `\n`
- Countdown Mode
  - Countdown in days to a specified date
- Added ability to setup WiFi connection on device
  - The device will set itself up as a Access Point (AP) on first start. You will be able to connect to this network and a web portal will be provided where you can setup the WiFi network you want to connect to
  - If the device was to lose connection, it should retry and if all else fails, it will open up the web portal again to change the WiFi settings if necessary
  - Option to be able use direct mode is also possible as previously implemented
- Reworked UI
  - Can see messages scheduled to be displayed and option to remove them
  - Loading indicators
  - Show hide information to fill out for the mode you select
  - See extra information on the UI such as:
    - Last Message Received
    - Number of Flaps registered
    - Version Number running
    - How many characters/lines are in the textbox for text
    - Add newline button (as typing `\n` is a pain on a mobile keyboard)
- Message Scheduling
  - Ability to send a message and display it at a later date.
  - Options for when a scheduled message being shown
    - If the clock was in another mode, such as `Clock` mode, it will show the message for a duration (changable via updating `scheduledMessageDisplayTimeMillis` in `ESPMaster.ino`), then return to that mode afterwards
    - A checkbox on the UI is presented ("Show Indefinitely") that when checked, will show a message and leave it on the display
- Arduino OTA
  - Over the Air updates to the display
- Updated `README.md` to add scenarios of problems encountered

Also the code has been refactored to try facilitate easier development:

- Changed serial prints to one central location so don't have to declare serial enable checks when a new one is required
- Renamed files and functions
- Ping endpoint
- Updated `data` so can test out project locally without having to call a webserver via a `localDevelopment` flag

3D-files here on [Printables](https://www.prusaprinters.org/prints/69464-split-flap-display)!

## General

The display's electronics use 1 x ESP01 (ESP8266) as the main hub and up to 16 Arduinos as receivers. The ESP handles the web interface and communicates to the units via I2C. Each unit is resposible for setting the zero position of the drum on startup and displaying any letter the main hub send its way.

Assemble everything according to the instruction manual which you can find on [GitHub](./Instructions/SplitFlapInstructions.pdf).

### PCB

Gerber files are in the `PCB` folder. These are the scehematics for the PCB boards and say, what is needed and where. You need one per unit. Populate components according to the [instruction manual](./Instructions/SplitFlapInstructions.pdf).

Options to potentially get boards created for you:

- [PCB Way](https://www.pcbway.com/)
- [JLC PCB](https://jlcpcb.com/)

> Note: Services are offered by these companies to assembly the boards for you. There are surface mounted components to these devices that you might not be able to do yourself like small resistors for instance, which must be flow soldered. It could be worth having the company do this aspect for you.

### Unit

Each split-flap unit consists of an Arduino Nano mounted on a custom PCB. It controls a 28BYJ-48 stepper motor via a ULN2003 driver chip. The drum with the flaps is homed with a KY003 hall sensor and a magnet mounted to the drum.

Upload the Arduino sketch `Unit.ino` in the unit folder to each unit's arduino nano. Before that set the offset with the `EEPROM_Write_Offset.ino` sketch.

Inside `Unit.ino`, there is a setting for testing the units so that a few letters are cycled through to ensure what is shown is what you expect. At the top of the file once you have opened the project, you will find a line that is commented out:

```c++
#define SERIAL_ENABLE   // uncomment for serial debug communication
#define TEST_ENABLE    	// uncomment for test mode where the unit will cycle a series of test letters.
```

> Note: If you experience any problems uploading the unit sketch, you may have to change your `Processor` to use the old bootloader, called `ATmega328p (Old Bootloader)`.

Remove the comment characters to help with your testing for the next step of Setting the Zero Position Offset.

#### Set Zero Position Offset

The zero position (or blank flaps position in this case) is attained by driving the stepper to the hall sensor and step a few steps forward. This offset is individual to every unit and needs to be saved to the arduino nano's EEPROM.

A simple sketch has been written to set the offset. Upload the `EEPROM_Write_Offset.ino` sketch and open the serial monitor with 115200 baudrate. It will tell you the current offset and you can enter a new offset. It should be around 100 but yours may vary. You may need to upload the `Unit.ino` sketch with the `TEST_ENABLE` flag uncommented and see if the offset is correct. Repeat until the blank flap is showing every time the unit homes.

#### Set Unit Address

Every units address is set by a DIP switch. They need to be set ascending from zero in binary.
This is how my 10 units are set, 1 means switch is in the up-position:
| Unit 1 | Unit 2 | Unit 3 | Unit 4 | Unit 5 | Unit 6 | Unit 7 | Unit 8 | Unit 9 | Unit 10 |
| ------ | ------ | ------ | ------ | ------ | ------ | ------ | ------ | ------ | ------- |
| 0000   | 0001   | 0010   | 0011   | 0100   | 0101   | 0110   | 0111   | 1000   | 1001    |

### ESP01/ESP8266

#### Pre-requisites

To upload the sketch to the ESP you need to install a few things to your arduino IDE.

- Install the ESP8266 board to your Arduino IDE. You can follow [this tutorial](https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/)
- Install the arduino ESP8266 littleFS plugin to use the file system of the ESP, you can follow [this tutorial](https://randomnerdtutorials.com/install-esp8266-nodemcu-littlefs-arduino/)
- Install the following libraries via Library Manager:
  - [ArduinoJSON](https://github.com/bblanchon/ArduinoJson) - Version: 7.0.4
  - [ESPAsyncWebSrv](https://github.com/dvarrel/ESPAsyncWebSrv) - Version: 1.2.7
    - Dependencies which should be installed automatically:
      - [ESPAsyncTCP](https://github.com/dvarrel/ESPAsyncTCP)
      - [AsyncTCP](https://github.com/dvarrel/AsyncTCP)
  - [NTPClient](https://github.com/arduino-libraries/NTPClient) - Version: 3.2.1
  - [ezTime](https://github.com/ropg/ezTime) - Version: 0.8.3
  - [LinkedList](https://github.com/ivanseidel/LinkedList) - Version: 1.3.3
  - [WiFiManager](https://github.com/tzapu/WiFiManager) - Version: 2.0.17

To upload sketches to the ESP8266 you can either use an [Arduino Uno](https://create.arduino.cc/projecthub/pratikdesai/how-to-program-esp8266-esp-01-module-with-arduino-uno-598166) or you can buy a dedicated programmer. It is highly recommend getting a programmer as it makes uploading programs onto the ESP8266 much faster.

> Note: Be wary of ESP8266 programmers that are available which allow USB connection to your PC which may not have programming abilities. Typically extra switches are available so that the ESP8266 can be put in programming mode, although you can modify the programmer through a simple solder job to allow it to enter programming mode. Examples can be found in the customer reviews of [Amazon](https://www.amazon.co.uk/gp/product/B078J7LDLY/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&th=1).

> Alternatively, you can get a dedicated programmer from Amazon such as [this one](https://www.amazon.co.uk/dp/B083QHJW21). This is also available on [AliExpress](https://www.aliexpress.com/item/1005001793822720.html?spm=a2g0o.detail.0.0.48622aefV0Zv89&mp=1) if you are willing to wait a while for it.

#### Uploading the Static Assets via LittleFS

There are static files located [here](./ESPMaster/data/) in the `data` folder of ESPMaster which will need to be uploaded. These make up the website that will be accessible on your WiFi so you can update the Split-Flap display.

Open the sketch `ESPMaster.ino` in the `ESPMaster` folder, change your board to "Generic ESP8266 Module", choose the correct COM-port and click:

> Tools -> ESP8266 LittleFS Data Upload

This uploads the website onto the ESP8266's file system.

**NOTE:** No sketch has been uploaded yet! Only the static files. At the time of writing, this will also only work on an older version of Arduino IDE < version 2. The latest Arduino IDE broke support for Plugins such as the LittleFS plugin.

#### Updating Settings of the Sketch

There are several options in the Sketch you can modify to customise or change the behaviour of the display. These are marked in the code as "Configurable".

By default, the system will run in an "Access Point" mode where you will be able to connect to the display and put in WiFi credentials directly. This means if you WiFi changes, you don't have to re-upload a new sketch. Screenshot of the WiFi setup portal:

![Screenshot WiFi Portal](./Images/Access-Point-Screenshot.jpg)

Alternatively, you can specify credentials directly. You can go ahead and change the credentials in these variables:

```c++
const char* wifiDirectSsid = "";
const char* wifiDirectPassword = "";
```

You will also need to change the WiFi Mode in the code via changing this variable to `true`:

```c++
//Option to either direct connect to a WiFi Network or setup a AP to configure WiFi. Default: false (puts device in AP mode)
#define WIFI_USE_DIRECT false
```

You will also want to change the `timezoneString` to your time zone. You can find the TZ database names here: https://en.wikipedia.org/wiki/List_of_tz_database_time_zones

You can also modify the date and clock format easily by using this table: https://github.com/ropg/ezTime#datetime

There are several helper `define` variables to help during debugging/running:

- **SERIAL_ENABLE**
  - Use this to enable Serial output lines for tracking executing code
- **OTA_ENABLE**
  - Use this to enable OTA updates from the Arduino IDE
  - Subsequently, you can set a password for OTA via the `otaPassword` variable
- **UNIT_CALLS_DISABLE**
  - Use this to disable the communication with the Arduino Nano Units. This will mean you can check code over function for the ESP module.

#### Experiments

In the main Sketch under "Configurable Defines", an "EXPERIMENTAL" section has been included. This section has been created for features that are things that can be changed and trialled however aren't going to be necessary to be changed for general use.

##### Static IP Address

A feature request of being able to set a Static IP Address was created by [beroliv](https://github.com/beroliv) (thank you for the suggestion). This was to get around issues whereby in some routers, there was issues in being able to do this.

Code has been added to be able to set a Static IP Address on device. To do this:

1. Set the `WIFI_STATIC_IP` variable to `true` (defaulted to `false`)
2. Update the following settings as necessary for your needs:

   ```c++
   IPAddress wifiDeviceStaticIp(192, 168, 1, 100);

   IPAddress wifiRouterGateway(192, 168, 1, 1);
   IPAddress wifiSubnet(255, 255, 0, 0);

   IPAddress wifiPrimaryDns(8, 8, 8, 8);
   ```

**Suggestion:** Set your device up with a Static IP via your router if possible and to avoid conflicts on your network, however feel free to run this code if you are not able to. Testing this functionality showed it does work in both AP/Direct WiFi modes.

#### Sketch Upload

So far we've only uploaded static files to the ESP8266. You now need to `Upload` the sketch to the ESP8266. Click on Upload and the ESP8266 will be upadted with the sketch and you are done. Stick the ESP8266 onto the first unit's PCB and navigate to the IP-address the ESP8266 is getting assigned from your router.

### Common Problems

- If the ESP is not talking to the units correctly, check the `UNITSAMOUNT` in the `ESPMaster.ino`. The amount of units connected has to match.
- ESP Serial debugging must be set to "false" for ESP01 to communicate to Nanos over I2C.
- Ensure you upload the sketch and the LittleFS sketch upload to the ESP8266.
- When the system is powered, your Hall Sensor should only light up when a magnet is nearby.
- Ensure you are running an older version of Arduino IDE to be able to upload static files to the device. You will need a version prior to version 2.x.
- User [@beroliv](https://github.com/beroliv) has reported having issues with WiFi connections. One solution they have proposed is soldering a wire to the antenna to be able to extend its range by creating an antenna. Here is the [link](https://www.stall.biz/project/verbesserte-wlan-konnektivitaet-mit-externen-antennen-fuer-wiffi-weatherman-und-andere-module-mit-esp8266/) (in German but Google Translate does a good job for other languages) they provided to detail the solution. Please take care when carrying out this solution. Thank you for the information [@beroliv](https://github.com/beroliv)!
