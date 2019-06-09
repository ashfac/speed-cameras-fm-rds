#include <Arduino.h>

#include <TVout.h>
#include <fontALL.h>

TVout TV;

void setup()  {
  Serial.begin(1000000); 

  TV.begin(NTSC, 104, 64);
  TV.select_font(font8x8ext);

  TV.clear_screen();
}

void loop() {
  
  if (Serial.available())
  {
    char ch = Serial.read();
    
    switch(ch)
    {
      case '!': TV.delay_frame(1); TV.clear_screen(); break;
      case '@': TV.set_cursor(0, 56); break;
      case '#': TV.set_cursor(58, 56); break;
      default:  TV.print(ch);
    }
  }
}
