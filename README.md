# VitalTracker: AI-Powered Realtime Health Monitor

![Project Status](https://img.shields.io/badge/status-active-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)
![Version](https://img.shields.io/badge/version-2.0-orange?style=for-the-badge)
![Hardware](https://img.shields.io/badge/hardware-ESP32%20+20MAX30102-0099CC?style=for-the-badge&logo=esp32)
![AI](https://img.shields.io/badge/AI-Google%20Gemini-ff6f00?style=for-the-badge&logo=google)
![Backend](https://img.shields.io/badge/backend-Firebase%20Realtime-FFCA28?style=for-the-badge&logo=firebase)

*VitalTracker* is an open-source, privacy-first realtime health monitoring system that combines IoT precision with empathetic artificial intelligence.

Using an ESP32 + MAX30102 sensor, it continuously tracks:
- Heart Rate (BPM)
- Blood Oxygen Saturation (SpO₂)
- Fatigue Index (proprietary algorithm)

All data streams instantly to a beautiful web dashboard, where *Google Gemini* (with a custom "Compassionate Caregiver" persona inspired by Baymax from Big Hero 6) provides calm, articulate, and medically-informed suggestions — never alarming, always supportive.

---

## 🌟 Key Features

| Feature | Description |
|---------|-------------|
| *Realtime Vitals Streaming* | Zero-latency updates via Firebase Realtime Database |
| *Empathetic AI Assistant* | Gemini 1.5 Flash/Pro with custom Baymax-style persona |
| *Advanced Fatigue Detection* | Proprietary algorithm combining HRV, SpO₂ trends & movement |
| *Animated Gauges & Heartbeat* | Smooth CSS + Canvas animations for immersive experience |
| *Daily Wellness Checklist* | Hydration, steps, meditation, sleep tracking with reminders |
| *Smart Alerts* | Detects hypoxia, tachycardia, bradycardia, sensor disconnect |
| *Mobile-First Design* | Fully responsive Tailwind CSS interface |
| *Offline Fallback Mode* | Continues local logging when Wi-Fi drops |
| *Privacy Focused* | All processing can run locally; cloud optional |

---

## 🛠 Tech Stack

| Layer | Technology |
|-------|------------|
| Hardware | ESP32-WROOM-32 • MAX30102 (I2C) • Optional MPU6050 for movement detection |
| Firmware | Arduino C++ • ESP32 Firebase-ESP-Client • MAX30102 libraries |
| Cloud | Firebase Realtime Database • Firebase Hosting (optional) |
| Frontend | HTML5 • Vanilla JS • Tailwind CSS (CDN) • Chart.js • Animate.css |
| AI | Google Gemini 1.5 Flash/Pro API (generative language) |
| Architecture | Edge (ESP32) → Cloud (Firebase) → Browser + AI on-demand |

---

## ⚙ System Architecture

mermaid
graph TD
    A[MAX30102<br/>Pulse Oximeter<br/>+<br/>MPU6050 Accelerometer] 
    -->|I2C| B[ESP32<br/>Microcontroller]
    B -->|Wi-Fi HTTPS| C[Firebase<br/>Realtime Database]
    C -->|WebSocket/REST| D[Web Dashboard<br/>Tailwind + Vanilla JS]
    D -->|On user request<br/>or auto-trigger| E[Google Gemini API<br/>"Compassionate Caregiver" persona]
    E -->|Calm, supportive advice| D
    B -.->|Local fallback| F[On-device SD logging]
    
---

## 🚀 Quick Start

### 1. Hardware Assembly


ESP32      → MAX30102
3.3V       → VCC
GND        → GND
D22 (SCL)  → SCL
D21 (SDA)  → SDA


### 2. Flash the ESP32
1. Open firmware/VitalTracker_ESP32/VitalTracker_ESP32.ino in Arduino IDE
2. Install libraries:
   - Firebase-ESP-Client by Mobizt
   - MAX3010x by oxullo
3. Set your Wi-Fi & Firebase credentials in secrets.h
4. Upload!

### 3. Deploy Web Dashboard


Just open index.html — everything is CDN-based!
Or host on Firebase Hosting / Netlify / Vercel for a public URL


### 4. Configure Gemini AI
- Get your API key from [Google AI Studio](https://aistudio.google.com/)
- Paste it in js/gemini.js
- (Optional) Customize the system prompt in js/prompts.js to make it even more Baymax-like as you want

---

## 📸 Screenshots

<img width="1415" height="881" alt="Screenshot 2025-12-07 135418" src="https://github.com/user-attachments/assets/d0eedaee-98c4-4e12-bd56-01dbad55c953" />


---

## 🎯 Example AI Responses (Baymax Mode)

> "Hello, I am Baymax, your personal healthcare companion. Your oxygen level is a little low at 94%. There is no need to worry. Please sit down, breathe slowly through your nose for 10 breaths, and I will stay with you until it improves. You are doing great."

> "Your heart rate has been elevated for 18 minutes. Would you like me to guide you through a 2-minute box breathing exercise? I am here to help."

---

## 🤝 Contributing

Contributions are very welcome! Whether it's improving the fatigue algorithm, adding new sensors (temperature, blood pressure), or making Baymax even more caring.

Please read CONTRIBUTING.md and submit PRs.

---

## 📄 License

This project is licensed under the *MIT License* - see the [LICENSE](LICENSE) file for details.

---

<div align="center">

*Made with ❤ and a little bit of Baymax magic*

<img src="https://fonts.gstatic.com/s/e/notoemoji/latest/1f916/512.webp" width="50" alt="Robot"/>

*"On a scale of 1 to 10, how would you rate your health?"*

</div>

Just replace the placeholder images in /assets/ with your actual screenshots, and you're good to go! This version is fully aligned, visually rich, and ready for GitHub. 🚀
