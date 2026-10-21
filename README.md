# Embedded-Security-Attack

## Requirements *(Hardware necessary to conduct your attack, as well as schematics describing their interfacing)*
- Computer
- 2 Arduinos (target & controller)
- Breadboard & wires
- 100kΩ, 10kΩ resistors & small ceramic capacitor
- 330µF, 100µF Capacitors & 100Ω resistor (these are optionnal)
- Oscilloscope

![diagram](/images/diagram.jpg)

## The Target *(Asset description)*
The target is an Arduino Uno board powered by the ATmega328P 8-bit AVR microcontroller.
The firmware implements an interactive command-line interface over UART (Serial) prompting users for a password. Upon successful validation, it computes and outputs a cryptographic hash of the secret keyphrase along with its associated salt.

# Attack Tree - ATmega328P (Arduino Uno) Vault - Timing Attack
 
```mermaid
graph TD
    GOAL["GOAL<br/>Recover protected hash + salt<br/>from vault firmware"]
 
    GOAL --> AND0{"AND"}
 
    AND0 --> RECON["1. Reconnaissance"]
    AND0 --> PHYS["2. Physical Access to MCU"]
    AND0 --> CLOCK["3. External Clock Control"]
    AND0 --> BRIDGE["4. Serial Bridge at Slowed Baud"]
    AND0 --> MEAS["5. Timing Measurement & Brute Force"]
 
    %% 1. Recon
    RECON --> R1["Extract strings from firmware binary<br/>(sisik.eu/elf)"]
    RECON --> R2["Identify prompt flow:<br/>'Enter password:' / ACCESS GRANTED-DENIED"]
    RECON --> R3["Probe default Serial (USB) port<br/>→ decoy 'not the main entrance'"]
    RECON --> R4["Sweep pins with logic analyzer/scope<br/>while power-cycling board"]
    R4 --> R5["Discover real UART on pins 10 & 11"]
 
    %% 2. Physical access
    PHYS --> P1["Desolder ATmega328P from Uno board"]
    P1 --> P2["Mount chip on breadboard<br/>for full pin access"]
 
    %% 3. Clock control
    CLOCK --> C1["Scope OSC1/OSC2 pins<br/>→ ~16 MHz, ~700mV p-p crystal signal"]
    C1 --> C2["Build 2nd 'controller' Arduino<br/>as fake clock generator"]
    C2 --> C3["Generate clock via interrupt timer<br/>+ voltage divider (100k/10k) + cap"]
    C3 --> C4["Inject fake clock into OSC1"]
    C4 --> C5["Validate technique<br/>on blank chip + blink sketch"]
    C5 --> C6["Choose 50kHz target clock<br/>→ chip runs ~320x slower"]
    C6 --> CLOCKDONE["Reflash chip with real firmware<br/>under controlled slow clock"]
 
    %% 4. Bridge
    BRIDGE --> B1["Compute effective baud:<br/>9600 / 320 ≈ 30 baud"]
    B1 --> B2["SoftwareSerial library?"]
    B2 --> B3["Hand-roll bit-banged<br/>UART TX/RX on controller Arduino"]
    B3 --> B4["Scope-calibrate frame timing<br/>to sync with 30-baud target"]
    B4 --> BDONE["USB (9600/115200) ⇄ 30 baud<br/>bidirectional bridge working"]
 
    %% 5. Measurement
    MEAS --> M1["Identify I/O pin that goes HIGH<br/>at start of password check,<br/>LOW after ACCESS DENIED"]
    M1 --> M2["Use pin edge as trigger<br/>to start/stop cycle counter"]
    M2 --> M3["Controller Arduino counts<br/>clock cycles per attempt"]
    M3 --> M4["Python script automates<br/>per-character password guessing"]
    M4 --> M5["Hypothesis:<br/>strcmp()-style early-exit compare"]
    M5 --> OUTCOME["RESULT: Approach validated,<br/>but comms too unstable<br/>(EM noise sensitivity) to complete<br/>full key recovery"]
 
    style GOAL fill:#7a1f1f,color:#fff
    style OUTCOME fill:#8a6d1f,color:#fff
    style CLOCKDONE fill:#1f5c3a,color:#fff
    style BDONE fill:#1f5c3a,color:#fff
```


## Vulnerability severity assessment
### CVSS v4.0 Score: 4.1 / Medium
- Macro vector: 211200
- Exploitability: Low
- Complexity: Low
- Vulnerable system: Medium
- Subsequent system: Low
- Exploitation: High
- Security requirements: High


We take the hypothesis that the target use "strcmp" or similar method to verify the password.  
So it should be vulnerable to a timing attack.

## Attack Demonstration *(Attack(s) description and implementation)*

We received an Arduino Uno board, and the goal was to break into it in order to recover a hash and a salt. We were also given the compiled firmware to flash onto the Arduino.

Before tackling the hardware itself, we did some preliminary research on the firmware we’d been given. [(download firmware)](/secure_sketch_v20251015.2.elf)  
By searching for all the strings present in the code (for example using the site https://sisik.eu/elf), 
we were able to extract the libraries used as well as several lines of text used somewhere in the program (here are a few examples):

```
f7-@Jp0w
Welcome to the vault. This is not the main entrance.
No input. Enter password:
Je suis une petite tortue
Here is your salt:
Here is your hash:
>> ACCESS GRANTED: running protected section
>> ACCESS DENIED
```

[(see the full file in “dump.txt”)](/dump.txt)

We then moved on to the physical attack on the Arduino.

We first tried to get into the Arduino through its serial communication port, the default method of communication between a computer and an Arduino. A welcome message read: “Welcome to the vault. This is not the main entrance.”

We then decided to desolder the ATmega328p from the Arduino board and place it on a breadboard, so we could access all the pins more comfortably. We restarted the ATmega repeatedly, each time measuring a different pin with the logic analyzer or oscilloscope. Using this technique, we found that pins 10 and 11 were also used for a serial communication line.

When we tried connecting to it, the message this time was: “Enter password:”

That was very encouraging. From this point on, we could start thinking about a specific attack method.

I decided to go with a timing attack, but with a twist. Instead of simply measuring elapsed time — which would be too imprecise — I planned to control the ATmega’s internal clock so I could precisely count the clock cycles needed to verify the password. Our working hypothesis was that the password check was done character by character using “strcmp” or a similar method with early-exit behavior.

After some research, it turned out the ATmega isn’t designed to run off an external clock unless it’s reprogrammed and specifically configured to do so. In my case, however, I couldn’t reprogram it since I didn’t have the source code, and in its current state it was set up to run off a quartz crystal.

ATmega328p Clock Modes Diagram                           |  ATmega328p Pinout
:-------------------------------------------------------:|:----------------------------------------------------------:
![atmega328p clock modes](/images/atmel328p_clock.png)   |  ![atmega328p pinout](/images/Atmega168PinMap2.png)

Still, I decided to try anyway. The ATmega datasheet tells us that the quartz clock circuit uses the OSC1 and OSC2 pins. So I used the oscilloscope to measure the input and output of the crystal to get a sense of the voltage and frequency ranges. We estimated the crystal clock circuit runs at around 16 MHz, with a voltage of about 700 mV peak-to-peak centered around 500 mV.

![Arduino Quartz](/images/osc2_1.png)

I planned to use a second Arduino board to simulate a clock signal and handle the communication. I used a 100k–10k voltage divider followed by a decoupling capacitor to step down the voltage from a pin on the “controller” Arduino, then fed that signal into the OSC1 input of the target ATmega to drive its clock.

Generated Clock Filtering                                |  ATmega Quartz Amplifier Retroaction
:-------------------------------------------------------:|:----------------------------------------------------------:
![Fake Clock Filtering](/images/fake_clock_divider.png)  |  ![Fake Clock Retroaction](/images/fake_clock_osc_1_2.png)
(CH1 Yellow : generated clock from arduino controller, CH2 Blue : after voltage divider filter) | (CH1 Yellow : OSC1 ATmega pin, CH2 Blue : OSC2 ATmega pin)

As a first test, to see whether the technique would even work, I flashed a blank ATmega with a simple blink program (LED blinking on and off).

To generate the clock signal, I used an Arduino interrupt timer. After running through some calculations [(see “clock_frequency_calculation.txt”)](/clock_frequency_calculation.txt), 50 kHz seemed like a good compromise: going faster would have caused issues on the controller Arduino’s side, since it struggled to generate interrupts at a higher rate; going slower would have been far too slow for serial communication with the target ATmega.

Going from the crystal’s 16 MHz down to the 50 kHz generated by the controller Arduino, we’d expect the blink program to run at 0.3125% of its normal speed, in other words : 320 times slower !

And despite everything I read online telling me this wouldn’t work and that I shouldn’t bother, it actually worked !  
In fact, it turned out to still work even after disconnecting the voltage divider entirely, with no physical electrical contact between the wires at all (likely due to inductive coupling or some similar phenomenon).

With the proof of concept validated, I was able to reflash the ATmega with the provided firmware to carry out the attack.

There was, however, a subtlety: now that the ATmega was only running at 50 kHz, it still “thought” it was communicating at 9600 baud, but it was actually communicating 320 times slower, so really at 30 baud. I therefore needed to write code for the controller Arduino that would bridge the USB communication (at 9600 or 115200 baud) down to the target ATmega’s 30 baud, in both directions (transmit and receive).

I first tried using the Arduino SoftwareSerial library, but it doesn’t handle such low baud rates well. So I wrote my own code using manual serial bit-banging, handling the serial communication “by hand” using arbitrary pins. I also used the oscilloscope to precisely analyze the timing and delays of the UART frames, so I could sync as closely as possible with the target ATmega’s timing.

![ATmega UART Communication](/images/tx_rx_b.png)

This method works, but it turned out to be very unstable and extremely sensitive to ambient electromagnetic noise. Depending on location, positioning, or even the time of day, we could get 10 successful reads in a row, or nothing but unreadable noise.
Even having the computer plugged to the power supply or not can drastically change the result's quality.

Console when everything works fine        |  Setup with shielding metal box
:----------------------------------------:|:----------------------------------------------------------:
![console ok](/images/screenshot_ok.png)  |  ![setup in metal box](/images/metal2.jpg)

I discovered that one of the ATmega’s pins went high at the start of the password verification and went low after the “access denied” message was transmitted. I therefore decided to use the transition of this pin to the high state as the trigger to start a clock-cycle counter, and to stop it as soon as the ATmega began responding on the serial communication channel.

I then implemented code on the controller Arduino to handle generating the target ATmega’s clock signal, counting those clock cycles, and managing communication between the computer and the target ATmega. I also wrote a Python program to automate password attempts letter by letter, measuring the time — or more precisely, the number of clock cycles — required each time before the ATmega responded.

In conclusion, due to the instability of the serial communication, I wasn’t able to carry the attack through to completion, but the underlying approach of manually controlling the target ATmega’s clock remains a success.

## Counter Measures

Assuming our attack had worked as intended, we can think about countermeasures to put in place to prevent this same attack from being effective/working in the future. One idea would be to use a secure verification function in which the validation time doesn’t depend on how much of the password is correct. Alternatively, a verification function could add a random delay to the check, so that multiple attempts with the same password yield completely different response times, making it impossible to use response time to guess the real password. Or simply disabling the secondary UART (pins 10/11).
 
```mermaid
graph TD
    M["Mitigate timing attack<br/>on password/hash check"]
    M --> M1["Use constant-time comparison<br/>(no early-exit strcmp)"]
    M --> M2["Add randomized delay<br/>to verification routine"]
    M --> M3["Disable secondary UART<br/>debug interface (pins 10/11)"]
```

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
