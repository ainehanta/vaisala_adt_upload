/*
 * sketch.ino
 *
 * Author:   Makoto Uju
 * Created:  2017-09-07
 */

#include <Ethernet.h>
#include <EthernetUdp.h>

#include <PFFIAPUploadAgent.h>
#include <TimeLib.h>
#include <LocalTimeLib.h>
#include <SerialCLI.h>
#include <ADT74x0.h>
#include <NTP.h>
#include <Wire.h>
#include <HMP155.h>

//cli
SerialCLI      commandline(Serial);
//  ethernet
MacEntry       mac("MAC", "01:23:45:67:89:AB", "mac address");
//  ip
BoolEntry      dhcp("DHCP", "true", "DHCP enable/disable");
IPAddressEntry ip("IP", "192.168.0.2", "IP address");
IPAddressEntry gw("GW", "192.168.0.1", "default gateway IP address");
IPAddressEntry sm("SM", "255.255.255.0", "subnet mask");
IPAddressEntry dns_server("DNS", "8.8.8.8", "dns server");
//  ntp
StringEntry    ntp("NTP", "ntp.nict.jp", "ntp server");
//  fiap
StringEntry    host("HOST", "202.15.110.21", "host of ieee1888 server end point");
IntegerEntry   port("PORT", "80", "port of ieee1888 server end point");
StringEntry    path("PATH", "/axis2/services/FIAPStorage", "path of ieee1888 server end point");
StringEntry    prefix("PREFIX", "http://j.kisarazu.ac.jp/Test/VAISALA01/", "prefix of point id");
//  debug
int debug = 0;

//ntp
NTPClient ntpclient;
TimeZone localtimezone = { 9*60*60, 0, "+09:00" };

//fiap
FIAPUploadAgent fiap_upload_agent;
char temperaturevd_str[16];
char temperaturevw_str[16];
char humidity_str[16];
char vpd_str[16];
char temperatureadt_str[16];
struct fiap_element fiap_elements [] = {
  { "TemperatureVD", temperaturevd_str, 0, &localtimezone, },
  { "TemperatureVW", temperaturevw_str, 0, &localtimezone, },
  { "HumidityV", humidity_str, 0, &localtimezone, },
  { "VPDV", vpd_str, 0, &localtimezone, },
  { "TemperatureADT", temperatureadt_str, 0, &localtimezone, },
};

//sensor
ADT74x0 tempsensor;
HMP155 vaisala(Serial2, 24);

void enable_debug()
{
  debug = 1;
}

void disable_debug()
{
  debug = 0;
}

void setup()
{
  int ret;
  commandline.add_entry(&mac);

  commandline.add_entry(&dhcp);
  commandline.add_entry(&ip);
  commandline.add_entry(&gw);
  commandline.add_entry(&sm);
  commandline.add_entry(&dns_server);

  commandline.add_entry(&ntp);

  commandline.add_entry(&host);
  commandline.add_entry(&port);
  commandline.add_entry(&path);
  commandline.add_entry(&prefix);

  commandline.add_command("debug", enable_debug);
  commandline.add_command("nodebug", disable_debug);

  commandline.begin(9600, "ADT74x0 Gateway");

  // ethernet & ip connection
  if(dhcp.get_val() == 1){
    ret = Ethernet.begin(mac.get_val());
    if(ret == 0) {
      restart("Failed to configure Ethernet using DHCP", 10);
    }
  }else{
    Ethernet.begin(mac.get_val(), ip.get_val(), dns_server.get_val(), gw.get_val(), sm.get_val());
  }

  // fetch time
  uint32_t unix_time;
  ntpclient.begin();
  ret = ntpclient.getTime(ntp.get_val(), &unix_time);
  if(ret < 0){
    restart("Failed to configure time using NTP", 10);
  }
  setTime(unix_time);

  // fiap
  fiap_upload_agent.begin(host.get_val(), path.get_val(), port.get_val(), prefix.get_val());

  // sensor
  Wire.begin();
  tempsensor.begin(0x48);

  Serial2.begin(4800, SERIAL_7E1);
  vaisala.begin();
}

void loop()
{
  static unsigned long old_epoch = 0, epoch;

  commandline.process();
  epoch = now();
  if(dhcp.get_val() == 1){
    Ethernet.maintain();
  }

  if(epoch != old_epoch){
    vaisala.read();
    dtostrf(vaisala.ta(), -1, 2, temperaturevd_str);
    debug_msg(temperaturevd_str);
    dtostrf(vaisala.tw(), -1, 2, temperaturevw_str);
    debug_msg(temperaturevw_str);
    dtostrf(vaisala.rh(), -1, 2, humidity_str);
    debug_msg(humidity_str);
    dtostrf(vaisala.vpd(), -1, 2, vpd_str);
    debug_msg(vpd_str);
    dtostrf(tempsensor.readTemperature(), -1, 2, temperatureadt_str);
    debug_msg(temperatureadt_str);

    if(epoch % 60 == 0){
      debug_msg("uploading...");

      for(int i = 0; i < sizeof(fiap_elements)/sizeof(fiap_elements[0]); i++){
        fiap_elements[i].time = epoch;
      }
      int ret = fiap_upload_agent.post(fiap_elements, sizeof(fiap_elements)/sizeof(fiap_elements[0]));
      if(ret == 0){
        debug_msg("done");
      }else{
        debug_msg("failed");
        Serial.println(ret);
      }
    }
  }

  old_epoch = epoch;
}

void debug_msg(String msg)
{
  if(debug == 1){
    Serial.print("[");
    print_time();
    Serial.print("]");
    Serial.println(msg);
  }
}

void print_time()
{
  char print_time_buf[32];
  TimeElements* tm;
  tm = localtime();
  sprintf(print_time_buf, "%04d/%02d/%02d %02d:%02d:%02d",
      tm->Year + 1970, tm->Month, tm->Day, tm->Hour, tm->Minute, tm->Second);
  Serial.print(print_time_buf);
}

void restart(String msg, int restart_minutes)
{
  Serial.println(msg);
  Serial.print("This system will restart after ");
  Serial.print(restart_minutes);
  Serial.print("minutes.");

  unsigned int start_ms = millis();
  while(1){
    commandline.process();
    if(millis() - start_ms > restart_minutes*60UL*1000UL){
      commandline.reboot();
    }
  }
}
