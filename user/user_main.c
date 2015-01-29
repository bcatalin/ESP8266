#include "osapi.h"
#include "user_interface.h"
#include "user_interface.h"

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "ip_addr.h"
#include "espconn.h"
#include "os_type.h"
//for sending data to uart like uart0_sendStr("Link\r\n");
#include "driver/uart.h" 

extern void ets_wdt_disable (void);

#define MAXTIMINGS 10000
#define BREAKTIME 20
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
static volatile os_timer_t some_timer;

static float lastTemp, lastHum;

static char hwaddr[6];

//----------------------------------------------------------------------
// Function: at_tcpclient_sent_cb
// Description: Call back for a disconnected client
// Params: None
// Return: None
//----------------------------------------------------------------------
static void ICACHE_FLASH_ATTR at_tcpclient_sent_cb(void *arg) {
    os_printf("sent callback\n");
     struct espconn *pespconn = (struct espconn *)arg;
     espconn_disconnect(pespconn);
}

//----------------------------------------------------------------------
// Function: at_tcpclient_discon_cb
// Description: Call back for a disconnected client
// Params: None
// Return: None
//----------------------------------------------------------------------
static void ICACHE_FLASH_ATTR at_tcpclient_discon_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;
    os_free(pespconn->proto.tcp);

    os_free(pespconn);
    os_printf("disconnect callback\n");

}
//----------------------------------------------------------------------
// Function: at_tcpclient_connect_cb
// Description: Call back for a connected client. Here data are sent to
//              server.
// Params: None
// Return: None
//----------------------------------------------------------------------
static void ICACHE_FLASH_ATTR at_tcpclient_connect_cb(void *arg)
{
  uart0_sendStr("at_tcpclient_connect_cb\r\n");
  struct espconn *pespconn = (struct espconn *)arg;

  os_printf("tcp client connect\r\n");
  os_printf("pespconn %p\r\n", pespconn);

  char payload[128];
  
  espconn_regist_sentcb(pespconn, at_tcpclient_sent_cb);
  espconn_regist_disconcb(pespconn, at_tcpclient_discon_cb);
  //~/bin/proxy curl "http://184.106.153.149/update?api_key=3QPPYHT1AEJO5VX&field1=33"  
  //os_sprintf(payload, "%s\r\n", "GET /update?api_key=3QPPYHT1AEJO5VX&field1=4000&field2=5000");   //T
  os_sprintf(payload, "GET /update?api_key=3QPPYHT1AEJO5VX&field1=%d&field2=%d\r\n", (int)(lastTemp*100), (int)(lastHum*100));   //T
  uart0_sendStr(payload);
  //os_sprintf(payload, MACSTR ",%d,%d\n", MAC2STR(hwaddr), (int)(lastTemp*100), (int)(lastHum*100));
  //os_printf(payload);
  //send payload to the SERVER
  espconn_sent(pespconn, payload, strlen(payload));
}
//----------------------------------------------------------------------
// Function: sendReading
// Description: sendReading to connected clients ????
// Params: t = temperatura h=humidity
// Return: None
//----------------------------------------------------------------------
static void ICACHE_FLASH_ATTR sendReading(float t, float h)
{
    char send_temp[20];
    os_sprintf(send_temp, "Temp:%d Hum:%d\r\n", (int)(t*100),(int)(h*100));
    //uart0_sendStr("sendReading\r\n");
    uart0_sendStr(send_temp);
    struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
    if (pCon == NULL)
    {
        os_printf("CONNECT FAIL\r\n");
        return;
    }
    pCon->type = ESPCONN_TCP;
    pCon->state = ESPCONN_NONE;
    
    pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    pCon->proto.tcp->local_port = espconn_port();
    //set up the server remote port
    pCon->proto.tcp->remote_port = 80;

    //set up the remote IP
    uint32_t ip = ipaddr_addr("184.106.153.149"); //IP address for thingspeak.com
    os_memcpy(pCon->proto.tcp->remote_ip, &ip, 4);
    
    //set up the local IP
    struct ip_info ipconfig;
    wifi_get_ip_info(STATION_IF, &ipconfig);
    os_memcpy(pCon->proto.tcp->local_ip, &ipconfig.ip, 4);

    //register at_tcpclient_connect_cb that will be called when the
    //connection with the thingspeak is done. In this call back function
    // will actualy do the data sending
    espconn_regist_connectcb(pCon, at_tcpclient_connect_cb);
    
    //espconn_regist_reconcb(pCon, at_tcpclient_recon_cb);

    //connect to the previous pCon created structure
    int ret = 0;
    ret = espconn_connect(pCon);
    //os_delay_us(500000);
    if(ret == 0) 
      uart0_sendStr("espconn_connect OK!\r\n");
    else
    {
      uart0_sendStr("espconn_connect FAILED!\r\n");  
      char *fail;
      os_sprintf(fail, "%d \r\n", ret);
      uart0_sendStr(fail);
    }


    os_printf("Temp =  %d *C, Hum = %d \%\n", (int)(t*100), (int)(h*100));
    
    //we will use these in the connect callback..
    lastTemp = t;
    lastHum = h;
}

//----------------------------------------------------------------------
// Function: readDHT
// Description: Read temp and humidity using the DHT22 sensor 
// Params: None
// Return: None
//----------------------------------------------------------------------
static void ICACHE_FLASH_ATTR readDHT(void *arg)
{
    uart0_sendStr("readDTH\r\n");
    
    //for now just send same random data. Not quite random but to test the
    //TCP client part
    //sendReading((float)1, (float)2);
    //return; //---> just for now
    
    int counter = 0;
    int laststate = 1;
    int i = 0;
    int j = 0;
    int checksum = 0;
    //int bitidx = 0;
    //int bits[250];

    int data[100];

    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    GPIO_OUTPUT_SET(2, 1);
    os_delay_us(250000);
    GPIO_OUTPUT_SET(2, 0);
    os_delay_us(20000);
    GPIO_OUTPUT_SET(2, 1);
    os_delay_us(40);
    GPIO_DIS_OUTPUT(2);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);


    // wait for pin to drop?
    while (GPIO_INPUT_GET(2) == 1 && i<100000) 
    {
        os_delay_us(1);
        i++;
    }

    if(i==100000)
         return;  //============>

    // read data!
      
    for (i = 0; i < MAXTIMINGS; i++) 
    {
        counter = 0;
        while ( GPIO_INPUT_GET(2) == laststate) 
        {
            counter++;
            os_delay_us(1);
            if (counter == 1000)
                break;
        }
        laststate = GPIO_INPUT_GET(2);
        if (counter == 1000) break;

        //bits[bitidx++] = counter;

        if ((i>3) && (i%2 == 0)) {
            // shove each bit into the storage bytes
            data[j/8] <<= 1;
            if (counter > BREAKTIME)
                data[j/8] |= 1;
            j++;
        }
    }

/*
    for (i=3; i<bitidx; i+=2) {
        os_printf("bit %d: %d\n", i-3, bits[i]);
        os_printf("bit %d: %d (%d)\n", i-2, bits[i+1], bits[i+1] > BREAKTIME);
    }
    os_printf("Data (%d): 0x%x 0x%x 0x%x 0x%x 0x%x\n", j, data[0], data[1], data[2], data[3], data[4]);
*/
    float temp_p, hum_p;
    if (j >= 39) {
        checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
        if (data[4] == checksum) 
        {
            /* yay! checksum is valid */
            
            hum_p = data[0] * 256 + data[1];
            hum_p /= 10;

            temp_p = (data[2] & 0x7F)* 256 + data[3];
            temp_p /= 10.0;
            if (data[2] & 0x80)
                temp_p *= -1;
            sendReading(temp_p, hum_p);  // -------> SEND DATA            
        }
    }

}
//----------------------------------------------------------------------
// Function: wifi_config
// Description: Configure ESP8266 with SSID and Password and set up the 
//              module to STATION mode
// Params: None
// Return: None
//----------------------------------------------------------------------
void wifi_config()
{
  uart0_sendStr("wifi_config\r\n");
  // Wifi configuration
  char ssid[32] = "WLAN_19";
  char password[64] = "xxxxxxxx";
  struct station_config stationConf;
 
  //Set station mode
  wifi_set_opmode( 0x1 );
 
  stationConf.bssid_set = 0;
 
  //Set ap settings
  os_memcpy(&stationConf.ssid, ssid, 32);
  os_memcpy(&stationConf.password, password, 64);
  wifi_station_set_config(&stationConf);
}


void user_init(void)
{

    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    //new code for the UDP server
    uart0_sendStr("user_init\r\n");
    //disable watch dog for now
    //ets_wdt_disable();
    //Set GPIO2 to output mode where the DHT22 sensor will be
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);

    wifi_config();
    //wifi_get_macaddr(0, hwaddr);

    os_timer_disarm(&some_timer);

    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)readDHT, NULL);

    //Arm the timer &some_timer is the pointer 1000 is the fire time in ms
    //0 for once and 1 for repeating. Now is set to 20 sec. 15 second is the
    //minimum interval accepted by the thinkspeak.com
    os_timer_arm(&some_timer, 20000, 1);   
    
}
