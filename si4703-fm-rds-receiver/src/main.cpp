#include <Arduino.h>

#include <SparkFunSi4703.h>
#include <Wire.h>
#include "DHT.h"
#include "constants.h"

#define DHTPIN 5
#define DHTTYPE DHT22
#define RDS_TIMEOUT 5000

int resetPin = 4;
int SDIO = A4;
int SCLK = A5;

#define CH_UP_PIN 2
#define CH_DN_PIN 3

Si4703_Breakout radio(resetPin, SDIO, SCLK);
DHT dht(DHTPIN, DHTTYPE);

int channel;
int volume;
uint8_t _signal_strength = 0;
char _rds[9];
char _rds_1[8];

typedef struct {
  uint8_t status;
  char start[9];
  char finish[9];
  char location[6];
} rds_t; 

uint8_t _current_rds_line = LINE_1;
uint8_t _previous_rds_line = LINE_1;
uint8_t _num_rds_lines = 0;

rds_t _rds_lines[NUM_LINES];
rds_t _rds_line_current;

unsigned long last_interrupt_time = 0;
unsigned long last_temp_time = 0;
unsigned long last_ss_time = 0;

#define CMD_NONE    0
#define CMD_SEEK_UP 1
#define CMD_SEEK_DN 2
#define CMD_VOL_UP  3
#define CMD_VOL_DN  4


uint8_t command = CMD_NONE;
bool first_half = true;

/*
ISR(USART_RX_vect)
{
    uint8_t r = UDR0;
    r = r; // silence compiler warnings
    command = r;
}
*/

// Function declarations
void serialEvent();
void ChannelUp();
void ChannelDn();
void VolumeUp();
void VolumeDn();
bool debounceKey();
void decodeRDS();
void extractTrafficInfo();
boolean isValidRDS(char rds[]);
void displayRDS();
void displayTrafficInfo();
boolean rdsRecordExists();
void scrollDisplayUp();
void displayRDSLine(uint8_t line, uint8_t index);
uint8_t incrementLineNumber(uint8_t current);
boolean verifyLocation(char* location);
void clearRDS();
void clearRDSLine(uint8_t line);
void toUpperCase(char* str);
void initRDSstruct();
void strcpy( char* dest, char* src );
void strncpy( char* dest, char* src, uint8_t count );
void displayChannelInfo();
void displayTemp();
void displaySignalStrength();


void setup()
{
  pinMode(CH_UP_PIN, INPUT);
  pinMode(CH_DN_PIN, INPUT);
  attachInterrupt(0, ChannelUp, FALLING);
  attachInterrupt(1, ChannelDn, FALLING);
  
  Serial.begin(1200);
  dht.begin();
  radio.powerOn();

  _rds[0] = 0;
  _rds_1[0] = 1;

  volume = 15;
  radio.setVolume(volume);
  channel = 1015; // 880; // 
  radio.setChannel(channel*10);
  Serial.write(CLR_SCREEN); // clear screen
  displayChannelInfo();
  
  Serial.write(LINE_3);
  Serial.print("SEARCHING FOR TRAFFIC");
}

void loop()
{
  switch( command )
  {
    case CMD_SEEK_UP:
      do{
        channel = radio.seekUp();
      }while( channel == 0 );
      displayChannelInfo();
      command = CMD_NONE;
      _rds[0] = 0;
      _rds_1[0] = 1;
      last_ss_time = 0;
      clearRDS();
      break;
      
    case CMD_SEEK_DN:
      do{
        channel = radio.seekDown();
      }while( channel == 0 );
      
      displayChannelInfo();
      command = CMD_NONE;
      _rds[0] = 0;
      _rds_1[0] = 1;
      last_ss_time = 0;
      clearRDS();
      break;
      
    case CMD_VOL_UP:
      VolumeUp();
      displayChannelInfo();
      command = CMD_NONE;
      break;
      
    case CMD_VOL_DN:
      VolumeDn();
      displayChannelInfo();
      command = CMD_NONE;
      break;
      
    default:
      displayTemp();
      displaySignalStrength();
      decodeRDS();
      break;
  }
}

void serialEvent()
{
  radio.cancelRDS();
  switch( Serial.read() )
  {
    case '9':
    case 'u':
    case '>':
    case '.':
      command = CMD_SEEK_UP;
      break;
      
    case '3':
    case 'd':
    case '<':
    case ',':
      command = CMD_SEEK_DN;
      break;

    case '+':
    case '=':
      command = CMD_VOL_UP;
      break;
      
    case '-':
      command = CMD_VOL_DN;
      break;
  }
}

void ChannelUp()
{
  if( debounceKey() )
  {
    radio.cancelRDS();
    command = CMD_SEEK_UP;
  }
}

void ChannelDn()
{
  if( debounceKey() )
  {
    radio.cancelRDS();
    command = CMD_SEEK_DN;
  }
}

void VolumeUp()
{
  volume ++;
  if (volume == 16) volume = 15;
  radio.setVolume(volume);
}

void VolumeDn()
{
  volume --;
  if (volume < 0) volume = 0;
  radio.setVolume(volume);
}

bool debounceKey()
{
  bool ret = false;
  unsigned long interrupt_time = millis();  
  if (interrupt_time - last_interrupt_time > 200)
  {
    ret = true;
  }
  
  last_interrupt_time = interrupt_time;  
  return ret;
}

void decodeRDS()
{
  radio.readRDS(_rds, RDS_TIMEOUT);
  if( isValidRDS( _rds ) && memcmp( _rds, _rds_1, 8 ) )
  {
    memcpy( _rds_1, _rds, 8 );
    toUpperCase(_rds);
    displayRDS();
    extractTrafficInfo();
  }
}

void extractTrafficInfo()
{
  if( ( *_rds == 'A' || *_rds == 'N' ) && *(_rds+1) >= '1' && *(_rds+1) <= '9' )
  {
    strncpy(_rds_line_current.start, _rds, 8);
    _rds_line_current.status = ST_START;
  }
  else if( _rds_line_current.status == ST_FINISH && *_rds == 'H' && *(_rds+1) == 'M' )
  {
    if( *(_rds+2) == 'P' )
      strncpy(_rds_line_current.location, _rds+4, 5);
    else
      strncpy(_rds_line_current.location, _rds+3, 5);
    
    if(verifyLocation(_rds_line_current.location))
    {
      _rds_line_current.status = ST_LOCATION;
      displayTrafficInfo();
    }
  }
  else if( _rds_line_current.status == ST_START || _rds_line_current.status == ST_FINISH )
  {
    if( *_rds == 'R' && *(_rds+1) == 'I' && *(_rds+2) == ' ' )
    {
      strncpy(_rds_line_current.finish, _rds+3, 5);
      _rds_line_current.status = ST_FINISH;
    }
    else
    {
      // this format: A79 RI, MAASTRIC, HMP 4.1
      char* p = _rds_line_current.start;
      
      // look for a space
      while( !(*p == ' ' || *p == '\0') )
        p++;
      
      if( *p == ' ' && *(p+1) == 'R' && *(p+2) == 'I' )
      {
        // remove the 'RI'
        *p = '\0';
        
        strncpy(_rds_line_current.finish, _rds, 8);
        _rds_line_current.status = ST_FINISH;
      }
      else if( _rds_line_current.status == ST_START )
      {
        strncpy(_rds_line_current.finish, _rds_1, 5);
        toUpperCase(_rds_line_current.finish);
        _rds_line_current.status = ST_FINISH;
      }
    }
  }
}

boolean isValidRDS(char rds[])
{
  for(uint8_t i = 0; i < 8; i++)
  {
    if( rds[i] == '\0' )
    {
      if( i == 0 )
        return false;
      else
        break;
    }
    
    if( rds[i] < ' ' || rds[i] > '~' ) // only printable ASCII characters
      return false;
  }
  return true;
}

void displayRDS()
{
    Serial.write(RDS_MAIN);
    Serial.print(_rds);
}

void displayTrafficInfo()
{
  uint8_t line;
  uint8_t index = _current_rds_line - LINE_1;
  
  if( _rds_line_current.start[0]    != '\0' && 
      _rds_line_current.finish[0]   != '\0' && 
      _rds_line_current.location[0] != '\0' &&
      _rds_line_current.status == ST_LOCATION &&
     !rdsRecordExists() )
  {
    if( _num_rds_lines == 0 )
    {
      clearRDSLine(CLR_LINE_3);
    }
    
    if( _num_rds_lines == NUM_LINES )
    {
      line = LINE_1 + NUM_LINES - 1;
      scrollDisplayUp();
    }
    else
    {
      line = _current_rds_line;
      clearRDSLine(line + CLR_LINE_1 - LINE_1);
      _num_rds_lines++;
    }
    
    memcpy( (void*)&_rds_lines[index], (void*)&_rds_line_current, sizeof( rds_t ) );
    displayRDSLine(line, index);
    _previous_rds_line = _current_rds_line;
    _current_rds_line = incrementLineNumber(_current_rds_line);
    _rds_lines[index].status = ST_INIT;
  }
}

boolean rdsRecordExists()
{
  for( uint8_t i = 0; i < NUM_LINES; i++ )
  {
    if( strcmp( _rds_lines[i].location, _rds_line_current.location ) == 0 && 
        strcmp( _rds_lines[i].finish, _rds_line_current.finish ) == 0 &&
        strcmp( _rds_lines[i].start, _rds_line_current.start ) == 0 )
    {
      return true;
    }
  }
  return false;
}

void scrollDisplayUp()
{
  uint8_t index;
  uint8_t line = _current_rds_line;

  clearRDSLine(CLR_LINE_ALL);
  for(uint8_t i = 0; i < NUM_LINES - 1; i++)
  {
    line = incrementLineNumber(line);
    index = line - LINE_1;
    
    displayRDSLine(LINE_1 + i, line - LINE_1);
  }
}

void displayRDSLine(uint8_t line, uint8_t index)
{
  Serial.write(line);
  Serial.print(_rds_lines[index].start);  Serial.print(' ');
  Serial.print(_rds_lines[index].finish); Serial.print(" ");
  Serial.print(_rds_lines[index].location);
}

uint8_t incrementLineNumber(uint8_t current)
{
  return LINE_1 + (current - LINE_1 + 1) % NUM_LINES;
}

boolean verifyLocation(char* location)
{
  // first digit should be a number
  if( *location == '\0' || !( *location >= '1' && *location <= '9' ) )
    return false;
  
  // look for decimal point
  while ( !( *location == '\0' || *location == '.' || !( *location >= '0' && *location <= '9' ) ) )
    location++;

  // there should always be a decimal point
  if( *location != '.' )
    return false;
    
  // there should be one digit after decimal point
  location++;
  if( !( *location >= '0' && *location <= '9' ) )
    return false;

  return true;
}

void clearRDS()
{
  Serial.write(RDS_MAIN);
  Serial.print("        "); // 8x spaces
}

void clearRDSLine(uint8_t line)
{
  Serial.write(line);
  
//  Serial.write(line);
//  Serial.print("                      "); // 22x spaces
}

void toUpperCase(char* str)
{
  char* ci = str;
  while(*ci != '\0')
  {
    if(*ci >= 'a' && *ci <= 'z')
      *ci = (*ci) - 32;
      
    ci++;
  }
}

void initRDSstruct()
{
  for( uint8_t i = 0; i < NUM_LINES; i++ )
  {
    _rds_lines[i].status = ST_INIT;
  }
}

void strcpy( char* dest, char* src )
{
  while(*src != '\0')
  {
    *dest = *src;
    src++;
    dest++;
  }
}

void strncpy( char* dest, char* src, uint8_t count )
{
  uint8_t i=0;
  while(i < count)
  {
    if(*src == '\0')
      *dest = ' ';
    else
      *dest = *src;
      
    src++;
    dest++;
    i++;
  }
  *dest = '\0';
}

void displayChannelInfo()
{
  Serial.write(CHANNEL_INFO);
  if( channel < 1000 )
    Serial.print(" ");
  if( channel < 100 )
    Serial.print(" ");
  Serial.print(channel/10); 
  Serial.print(".");
  Serial.print(channel%10); 
  Serial.print(" FM");
}
  
void displayTemp()
{
  unsigned long current_time = millis();  
  if( !last_temp_time || ( current_time - last_temp_time ) > 10000 )
  {
    // temp humidity
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if ( isnan(h) || isnan(t) )
    {
    }
    else
    {
      Serial.write(TEMP_HUMID);
      Serial.print(t,1); Serial.write(128); Serial.print("C ");
      Serial.print(h,1); Serial.print("%");
    }
    last_temp_time = current_time;
  }
}

void displaySignalStrength()
{
  unsigned long current_time = millis();  
  if( !last_ss_time || ( current_time - last_ss_time ) > 5000 )
  {
    uint8_t ss = radio.getSignalStrength();

    if( _signal_strength != ss )
    {
      _signal_strength = ss;
      Serial.write(SIGNAL_STRENGTH);
      Serial.print("   "); // clear the line
      Serial.write(SIGNAL_STRENGTH);
      Serial.print(ss); // print the signal strength
    }
    last_ss_time = current_time;
  }
}
  
