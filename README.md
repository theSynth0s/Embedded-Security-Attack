# Embedded-Security-Attack

## Requirements *(Hardware necessary to conduct your attack, as well as schematics describing their interfacing)*
- Computer
- 2 Arduinos (target & controller)
- Breadboard & wires
- Optionnal : 100k, 10k resistors & small ceramic capacitor
- Oscilloscope

## The Target *(Asset description)*
The target is an Arduino Uno board powered by the ATmega328P 8-bit AVR microcontroller.
The firmware implements an interactive command-line interface over UART (Serial) prompting users for a password. Upon successful validation, it computes and outputs a cryptographic hash of the secret keyphrase along with its associated salt.

## Attack Tree — ATmega328P (Arduino Uno) — Timing Attack
 
```mermaid
graph TD
    ROOT["Root goal<br/>Obtain the Hash + Salt<br/>stored in the ATmega328P"]
 
    ROOT --> A["A. Direct extraction<br/>of flash/EEPROM memory"]
    ROOT --> B["B. Exploiting the<br/>'password' serial interface"]
 
    %% ---------- BRANCH A ----------
    A --> A1["A1. Read via ISP (SPI)"]
    A1 --> A1cond{{"AND"}}
    A1cond --> A1a["Lock bits fuses = 0<br/>(flash unprotected)"]
    A1cond --> A1b["Access to<br/>MOSI/MISO/SCK/RESET pins"]
 
    A --> A2["A2. Bypassing<br/>lock bits"]
    A2 --> A2a["Partial chip erase /<br/>glitch during erase"]
    A2 --> A2b["Decapping + optical/<br/>microprobe reading (invasive)"]
 
    A --> A3["A3. Is debugWIRE active?"]
    A3 --> A3cond{{"AND"}}
    A3cond --> A3a["DWEN fuse = enabled"]
    A3cond --> A3b["debugWIRE adapter<br/>on RESET pin"]
    A3cond --> A3out["-> Memory dump +<br/>hardware single-step"]
 
    %% ---------- BRANCH B ----------
    B --> B1["B1. Naive brute force<br/>(all combinations)"]
    B1 --> B1note["Impractical alone:<br/>key space too large"]
 
    B --> B2["B2. Timing attack via<br/>manual clock control"]
 
    B --> B3["B3. Power analysis<br/>(SPA/DPA) during comparison"]
 
    B --> B4["B4. Fault injection<br/>(clock/voltage glitch)<br/>to skip verification"]
 
    %% ---------- DETAIL OF THE CHOSEN BRANCH B2 ----------
    B2 --> B2_1["B2.1 Protocol reconnaissance<br/>(baud rate, frame, messages)"]
 
    B2 --> B2_2["B2.2 Replacing the clock<br/>with a controllable external source"]
    B2_2 --> B2_2cond{{"AND"}}
    B2_2cond --> B2_2a["Stable external clock<br/>(generator/companion MCU)"]
    B2_2cond --> B2_2b["Stable power supply/reset<br/>at startup"]
 
    B2 --> B2_3["B2.3 Counting clock cycles<br/>(single-step or timing measurement)"]
 
    B2 --> B2_4["B2.4 Timing oracle<br/>character by character"]
    B2_4 --> B2_4a["Hypothesis: sequential<br/>comparison is not constant-time"]
    B2_4 --> B2_4b["For each position:<br/>test candidate values,<br/>keep the one with most cycles"]
    B2_4 --> B2_4c["Repeat position by position<br/>until the full password is recovered"]
 
    B2 --> B2_5["B2.5 Measurement reliability"]
    B2_5 --> B2_5a["Average/median over<br/>multiple trials per candidate"]
    B2_5 --> B2_5b["Control temperature/<br/>supply voltage"]
 
    B2 --> B2_6["B2.6 Final validation"]
    B2_6 --> B2_6out["-> Send the reconstructed password<br/>-> Receive Hash + Salt"]
 
    %% ---------- STYLES ----------
    classDef chosen fill:#2e7d32,stroke:#1b5e20,color:#fff,stroke-width:2px;
    classDef cond fill:#fff3cd,stroke:#997404,color:#000;
    classDef root fill:#1565c0,stroke:#0d47a1,color:#fff,stroke-width:2px;
 
    class ROOT root;
    class B2,B2_1,B2_2,B2_3,B2_4,B2_5,B2_6,B2_6out,B2_4a,B2_4b,B2_4c,B2_5a,B2_5b,B2_2a,B2_2b chosen;
    class A1cond,A3cond,B2_2cond cond;
```

(Vulnerability severity assessment)
We take the hypothesis that the target use "strcmp" or similar method to verify the password.
So it should be vulnerable to a timing attack.

## Attack Demonstration *(Attack(s) description and implementation)*

(Proposed counter-measure(s))
