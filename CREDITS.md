## Credits & References

### Hardware / Datasheets
- [ATmega328P Datasheet (Microchip)](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf) — pinout, clock modes (external clock vs. crystal oscillator), UART timing
- [ATmega168 Hardware (Arduino retired docs)](https://docs.arduino.cc/retired/hacking/hardware/Atmega168Hardware/) — AVR hardware internals shared with the ATmega328P

### Arduino / Software References
- [Port Manipulation (Arduino retired docs)](https://docs.arduino.cc/retired/hacking/software/PortManipulation/) — direct register access, used for manual bit-banging of the serial lines
- [SoftwareSerial Library (Arduino docs)](https://docs.arduino.cc/learn/built-in-libraries/software-serial/) — initial (unsuccessful) approach for bridging the ultra-low baud rate

### Firmware Analysis Tools
- [sisik.eu/elf](https://sisik.eu/elf) — online ELF string/symbol extraction, used for initial firmware reconnaissance

### Related Work
- [Embedded-Security (parastuffs)](https://github.com/parastuffs/Embedded-Security) — related embedded security coursework/projects

### Further Reading (suggested additions)
- [AVR Instruction Set Manual (Microchip)](https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-Instruction-Set-Manual-DS40002198.pdf) — for reasoning about clock-cycle counts per instruction
- Kocher, P. — *Timing Attacks on Implementations of Diffie-Hellman, RSA, DSS, and Other Systems* (1996) — foundational paper on timing side-channel attacks, relevant background for the `strcmp`-timing hypothesis
- [Side-channel attack — Wikipedia](https://en.wikipedia.org/wiki/Side-channel_attack) — general background/terminology
