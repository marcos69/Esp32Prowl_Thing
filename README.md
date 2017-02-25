# Esp32Prowl_Thing
Esp32 idf-sdk Prowl notification / deep sleep demo.

Sends VBat via Prowl notification, blink and go to deepsleep.
Push Button (or GPIO 0..) to wake and restart.

On a Sparfun Esp32 Thing board,
make a voltage divider: 4.1V->3.3V
(ADC pin Vmax= 3.3V ! Be carefull!)
add a 100K resistor between GND and Pin 35 
and a 27K resistor between Vbat and Pin 35

Don't forget to add the APIKEY from Prowl, see EspProwl.
