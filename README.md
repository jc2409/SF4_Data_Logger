# Smart Guitar Amp: LLM-Driven Analog & Digital Effects

## Overview
The Smart Guitar Amp is a hybrid hardware-software system that bridges the gap between pure analog tone, digital signal processing (DSP), and modern artificial intelligence. It takes a raw, high-impedance guitar signal and processes it through a custom-designed analog front-end, followed by a digital effects pipeline.

Instead of manually turning physical knobs, users interact with a web interface powered by a Large Language Model (LLM). By entering natural language prompts (e.g., "Make me sound like David Gilmour on Comfortably Numb") or searching for specific songs, the AI translates subjective tonal requests into objective numerical parameters. These parameters automatically adjust both the physical analog circuit and the digital DSP algorithms in real-time to match the desired sound.

---

## System Architecture

The project is divided into three main computational domains:
1. **Analog Front-End:** High-fidelity signal conditioning and optical gain staging.
2. **Embedded Firmware & DSP:** Real-time data acquisition, digital audio transformations, hardware control, and communication.
3. **Software Backend & UI:** User interaction, LLM parameter translation, and preset management.

---

## 1. Analog Front-End (Audio Processing)
The analog circuit is designed to operate on a single 5V supply while preserving the integrity and dynamic range of the AC guitar signal. The audio path prepares the signal for digitization while providing analog drive characteristics controlled optically by the microcontroller.

The circuit consists of three primary, AC-coupled stages:

### Stage 1: Input Buffer & Power Conditioning
* **Op-Amp:** TL072 (JFET-input)
* **Function:** Provides a high-impedance (1MΩ) input to prevent loading the passive guitar pickups, preserving high-frequency transient responses. 
* **Virtual Ground Generation:** The unused second channel of the TL072 acts as an active unity-gain buffer to generate a rock-solid 2.5V DC reference from a filtered voltage divider, preventing power rail sag and inter-stage crosstalk.
* **Coupling:** The raw signal is AC-coupled via a 10µF capacitor and biased to the 2.5V reference to maximize dynamic range.

### Stage 2: Variable Gain Amplifier (VGA)
* **Op-Amp:** LM358 / LM2904
* **Function:** Amplifies the buffered guitar signal to introduce analog warmth and drive.
* **Smart Control:** The feedback loop utilizes a Light Dependent Resistor (LDR). By coupling this LDR with an LED controlled by the microcontroller, the firmware optically adjusts the analog gain. This completely isolates the audio path from digital control signals, preventing noise injection.

### Stage 3: Low-Pass Filter (Anti-Aliasing)
* **Op-Amp:** LTC6078 (Precision Rail-to-Rail)
* **Function:** A Sallen-Key low-pass filter topology.
* **Signal Integrity:** The signal is AC-coupled (4.7µF) from the VGA and actively pulled back to the 2.5V reference via a 1MΩ resistor. The filter strictly removes high-frequency noise and out-of-band harmonics to prevent aliasing before the signal reaches the microcontroller's Analog-to-Digital Converter (ADC).

---

## 2. Embedded Firmware & DSP
The firmware acts as the execution engine for the smart amplifier. It handles continuous data acquisition, applies mathematical transformations to the audio array, actuates the analog hardware, and maintains communication with the software backend.

### Data Acquisition (ADC)
* **Interrupt-Driven Sampling:** The ADC is triggered by a hardware timer to guarantee a rigid, deterministic sample rate, rather than relying on blocking software loops.
* **Buffering:** Incoming samples are streamed directly into double-buffered arrays (or via DMA where supported) to allow the CPU to process one block of audio while the next block is actively recording.

### Signal Processing Pipeline (DSP)
Once digitized, the audio array passes through an interrupt-driven pipeline to achieve distinct digital effects:
* **Non-Linear Transformations (Overdrive/Fuzz):**
  * **Hard/Soft Clipping:** Implements strict threshold logic to truncate waveforms, generating odd-order harmonics. Soft clipping utilizes wave-shaping algorithms (hyperbolic tangent approximations or polynomial mapping) to simulate tube compression.
* **Time-Domain Processing (Delay/Chorus):**
  * **Circular Ring Buffers:** Stores audio samples in a pre-allocated memory array. 
  * **Fractional Delay & LFOs:** Modulates the read-pointer of the ring buffer using Low Frequency Oscillators. Linear interpolation is applied between samples to prevent pitch artifacts when reading at fractional indices.
* **Frequency-Domain Processing (EQ):**
  * **Biquad Filters:** Implements cascaded Infinite Impulse Response (IIR) filters. The LLM dictates the coefficients (Q-factor, center frequency, and gain) to create parametric EQ bands.

### Effect Actuation (Hardware Control)
* **Optical Gain Staging:** The firmware translates the target analog gain into a high-speed PWM signal. This PWM frequency is set well above the human hearing range (>20kHz) and heavily RC-filtered into a stable DC voltage to drive the LED in the VGA stage. This ensures zero switching noise bleeds into the LDR and the audio path.

### Serial Communication Protocol
* **Non-Blocking UART:** The firmware maintains a high baud-rate UART connection with the host machine. 
* **Payload Parsing:** Incoming JSON payloads from the backend are parsed asynchronously. This ensures that adjusting parameters—like changing a biquad coefficient or updating the PWM duty cycle for the analog stage—never stalls the critical audio sampling interrupts.

---

## 3. Software & AI Architecture
The software stack provides the intelligence of the amplifier, handling user intent, LLM prompting, and serial communication with the embedded hardware.

### Tech Stack
* **Frontend:** React / Next.js
* **Backend:** Python / FastAPI
* **Database:** Supabase 

### Core Features
* **Natural Language Tone Engine:** Users submit descriptive requests ("make it sound muddy but sharp"). The Python backend constructs a prompt including the current system capabilities and queries the LLM. The AI returns a structured JSON payload of objective DSP coefficients and analog gain values.
* **Song/Artist Search:** A database-driven search function. When a user requests a specific song, the system retrieves known tonal metadata (amp type, specific pedal settings) and applies the corresponding DSP transformations and LDR gain stages.
* **Hardware Translation Layer:** A lightweight local service takes the generated JSON payload from the backend and streams the encoded parameters over the Serial connection to the microcontroller's registers.
* **Digital Dashboard:** A real-time UI reflecting the physical and digital state of the amplifier. Users can manually adjust the AI's suggested filter cutoffs, clipping thresholds, or delay times, and save these refined setups to the database as custom presets.