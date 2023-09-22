# VOC Dualcore



## BOM

ESP32-CAM

Sensirion SHT45

Sensirion SGP41

1602 LCD (I2C)



## Features

*   Using the TIG stack, i.e. telegraf + InfluxDB + grafana to store and present historical data;
*   使用 TIG stack，即 telegraf + InfluxDB + grafana 存储和展示历史数据;



*   The TIG stack is deployed by Docker compose;
*   TIG stack 使用 Docker compose 完成部署;



*   Using the dual-core simultaneously of the ESP32-cam;
    *   Core 0 is responsible for acquiring data and displaying it on the LCD (local hardware control); 
    *   core 1 takes the responsibilities of uploading data to the TIG stack;

*   使用 ESP32-cam 的双核心硬件;
    *   核心0负责采集数据并将数据显式在 LCD 上，即负责本地硬件的控制;
    *   核心1负责向TIG stack上报数据;



*   Multiple WiFi access points can be added to try to ensure data acquired can be uploaded to the stack;
*   可添加多个 WiFi 接入点，尽力保证数据可以上传;



*   Sync local time automatically via connecting to the public NTP server;
    *   Sync loacl time at the startup;
    *   Sync local time every 22 hours (80000 seconds);
*   连接公网 NTP 服务器，自动同步时间;
    *   初次启动时同步时间;
    *   约每22小时 (80000秒) 同步时间;



## Notes

*   使用开发板双核芯特性时无需写loop函数体;

*   1602 LCD 需搭配特定版本的库;



## TODO

*   本地存储网络丢失期间的数据，待连接有效后再上传数据;
*   使用 MQTT 协议上传数据;
*   VOC 对时间积分，实现个人 VOC 剂量仪;



## Flaws

*   SGP41 所测得的信号本质上相当于气味，与 VOC 并不表现为线性关系;
*   SGP41 的钝化问题 (如入鲍鱼之肆，久而不闻其臭);
