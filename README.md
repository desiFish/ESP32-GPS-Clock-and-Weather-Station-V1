<div align="center">  
  <h1> 🌟 ESP32 GPS Clock & Weather Station </h1>

  [![License](https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=for-the-badge)](LICENSE)
  [![ESP32](https://img.shields.io/badge/ESP32-Developer-blue?style=for-the-badge&logo=espressif)](https://www.espressif.com/)
  [![Arduino](https://img.shields.io/badge/Arduino-Compatible-green?style=for-the-badge&logo=arduino)](https://www.arduino.cc/)
  
  [![Status](https://img.shields.io/badge/Status-Active-success?style=for-the-badge)](https://github.com/desiFish/ESP32-GPS-CLOCK-V1)
  [![Issues](https://img.shields.io/github/issues/desiFish/ESP32-GPS-CLOCK-V1?style=for-the-badge)](https://github.com/desiFish/ESP32-GPS-CLOCK-V1/issues)

<a href="https://www.flaticon.com/free-icons/clock" title="clock icons">Clock icons created by Those Icons - Flaticon</a>
  <p align="center">
    <i>A precision timepiece that syncs with satellites and monitors your environment! 🛰️</i>
  </p>
</div>

<hr style="border: 2px solid #f0f0f0; margin: 30px 0;">


  ><h2>⚠️ Enhanced Version Available ⚠️</h2>
  ><p>An improved version of this project with physical buttons for better device configuration is available at:<br>
  ><a href="https://github.com/desiFish/ESP32-GPS-CLOCK-V2">ESP32-GPS-CLOCK-V2</a></p>
  ><hr style="border: 2px solid #f0f0f0; margin: 20px 0;">


<h2>✨ Features</h2>
<table>
  <tr>
    <td>🕒 Time</td>
    <td>GPS-synchronized precise timekeeping</td>
  </tr>
  <tr>
    <td>🌡️ Environment</td>
    <td>Temperature & humidity monitoring</td>
  </tr>
  <tr>
    <td>🔆 Display</td>
    <td>Auto-brightness & power saving</td>
  </tr>
  <tr>
    <td>📱 Connectivity</td>
    <td>WiFi with OTA updates</td>
  </tr>
</table>

<div align="center" style="background: linear-gradient(45deg, #1a1a1a, #2a2a2a); padding: 20px; border-radius: 10px; margin: 20px 0;">
  <h2 style="color: white;">🌟 Project Highlights</h2>
  <table style="background: rgba(255,255,255,0.1); border-radius: 8px;">
    <tr>
      <td>⚡ Fast GPS Lock</td>
      <td>🌙 Auto Brightness</td>
      <td>🔄 OTA Updates</td>
    </tr>
    <tr>
      <td>📱 WiFi Manager</td>
      <td>🔋 Battery Backup</td>
      <td>🌡️ Environment Monitor</td>
    </tr>
  </table>
</div>

<h2>🛠️ Hardware Requirements</h2>

<details>
<summary><strong>Core Components 📋</strong></summary>

| Component | Purpose | Notes |
|-----------|---------|--------|
| ESP32 devkit v1 | 🧠 Controller | DOIT version recommended |
| BH1750 | 💡 Light sensor | I²C interface |
| BME280 | 🌡️ Environment | Temperature/Humidity |
| GPS Neo 6m | 📡 GPS receiver | UART interface |
| ST7920 LCD | 🖥️ Display | 128x64 pixels |
| Buzzer | 🔊 Alerts | Active buzzer |

</details>

<details>
<summary><strong>Optional Components 🔧</strong></summary>

- 🔋 LiFePO4 AAA 80mAh (GPS backup)
- ⚡ TP5000 charging circuit
- 🔌 BMS with IN4007 diode
- 🛠️ Prototyping materials

</details>

<details>
<summary><strong>Required Libraries 📚</strong></summary>

| Library | Source | Notes |
|---------|--------|-------|
| ASyncTCP | [ESP32Async](https://github.com/ESP32Async/AsyncTCP) | Latest version |
| ESPAsyncWebServer | [ESP32Async](https://github.com/ESP32Async/ESPAsyncWebServer) | Latest version |

> ℹ️ **Note:** We use the libraries from ESP32Async repository as they are regularly maintained and updated.

</details>

<div style="background-color: #f8f9fa; border-radius: 10px; padding: 20px; margin: 20px 0;">
  <h3>🎯 Key Features Explained</h3>
  <ul>
    <li>🕒 <strong>Precise Timekeeping:</strong> GPS-synchronized time with battery backup</li>
    <li>🌡️ <strong>Environmental Monitoring:</strong> Temperature and humidity tracking</li>
    <li>📱 <strong>Smart Connectivity:</strong> WiFi enabled with web configuration</li>
    <li>🔆 <strong>Adaptive Display:</strong> Auto-brightness with power saving</li>
  </ul>
</div>

<h2>⚠️ Important Notice</h2>
<div style="background-color: #fff3cd; padding: 10px; border-radius: 5px; border-left: 5px solid #ffeeba;">
  <strong>❌ DO NOT USE AHT25 SENSOR!</strong><br>
  Due to significant accuracy issues, we recommend using BME280/BMP280/TMP117 instead.
</div> <br>
<strong>🔋 GPS Battery Modification</strong>
<div style="background-color: #f8f9fa; padding: 15px; border-radius: 5px; margin-top: 10px;">
  <h4>⚠️ Known Issue with GPS Module's Internal Battery</h4>
  <p>
    The NEO-6M GPS modules often come with problematic internal rechargeable batteries that:
    <ul>
      <li>Are frequently dead on arrival</li>
      <li>Fail to hold charge properly</li>
      <li>Only last 15-20 minutes when disconnected</li>
      <li>Cannot be reliably recharged</li>
    </ul>
  </p>

  <h4>🛠️ Solution Implemented</h4>
  <p>
    To resolve this, I've made the following modifications:
    <ul>
      <li>Removed the internal battery and charging diode</li>
      <li>Installed a LiFePO4 battery (AAA size)</li>
      <li>Added TP5000 charging circuit for reliable charging</li>
      <li>Implemented BMS for deep discharge protection</li>
      <li>Added diode to drop voltage to 3V for GPS backup pin</li>
    </ul>
  </p>

  <h4>💡 User Options</h4>
  <div style="background-color: #e2e3e5; padding: 10px; border-radius: 5px;">
    <strong>You have two choices:</strong>
    <ol>
      <li><strong>Keep Original Battery:</strong> 
        <ul>
          <li>Suitable if clock remains powered most of the time</li>
          <li>No modifications needed</li>
        </ul>
      </li>
      <li><strong>Modify Battery (Recommended):</strong>
        <ul>
          <li>Better for frequent power cycles</li>
          <li>Eliminates 5-10 minute GPS lock delay on cold starts</li>
          <li>More reliable long-term solution</li>
        </ul>
      </li>
    </ol>
  </div>
</div>

<h2>📊 Schematics</h2>
<div align="center" style="background-color: #f8f9fa; padding: 20px; border-radius: 8px; margin: 20px 0;">
    <strong>👀 Circuit Diagram</strong>
    <br>
    <img src="https://github.com/KamadoTanjiro-beep/ESP32-GPS-CLOCK-V1/blob/main/resources/schematic/Schematic_GPSClock-V1.png" 
         alt="GPS Clock Schematic" 
         width="600px"
         style="border-radius: 5px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
    <p><em>ESP32 GPS Clock Circuit Diagram</em></p>

  <table>
    <tr>
      <td>📝 Format</td>
      <td>High-resolution PNG</td>
    </tr>
    <tr>
      <td>🔍 Zoom</td>
      <td>Click image to enlarge</td>
    </tr>
    <tr>
      <td>💾 Download</td>
      <td><a href="https://github.com/KamadoTanjiro-beep/ESP32-GPS-CLOCK-V1/blob/main/resources/schematic/Schematic_GPSClock-V1.png">Full Resolution</a></td>
    </tr>
  </table>
</div>

<h2>📸 Gallery</h2>
<div align="center">
  <table>
    <tr>
      <!-- First Row -->
      <td width="33%">
        <img src="resources/images/x1.jpg" 
             alt="Internal Components" 
             width="100%"
             style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
        <p><em>Internal Components 🔧</em></p>
      </td>
      <td width="33%">
        <img src="resources/images/x2.jpg" 
             alt="PVC Case" 
             width="100%"
             style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
        <p><em>PVC Case 🎨</em></p>
      </td>
      <td width="33%">
        <img src="resources/images/x3.jpg" 
             alt="Front View" 
             width="100%"
             style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
        <p><em>Front View 📱</em></p>
      </td>
    </tr>
    <tr>
      <!-- Second Row -->
      <td width="33%">
        <img src="resources/images/x4.jpg" 
             alt="PCB Layout" 
             width="100%"
             style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
        <p><em>PCB Layout 🔌</em></p>
      </td>
      <td width="33%">
        <img src="resources/images/x5.jpg" 
             alt="Internal View 2" 
             width="100%"
             style="border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1);">
        <p><em>Internal View 2 🔌</em></p>
      </td>
      <td width="33%"></td>
    </tr>
  </table>
</div>

<h2>📜 License</h2>
<div style="background-color: #e9ecef; padding: 15px; border-radius: 5px;">
<h3>GNU General Public License v3.0</h3>

<h4>✅ Permissions</h4>
<ul>
  <li>Commercial use</li>
  <li>Modification</li>
  <li>Distribution</li>
  <li>Patent use</li>
  <li>Private use</li>
</ul>

<h4>⚠️ Conditions</h4>
<ul>
  <li><strong>License and copyright notice:</strong> Include the original license and copyright</li>
  <li><strong>State changes:</strong> Document all modifications</li>
  <li><strong>Disclose source:</strong> Make source code available</li>
  <li><strong>Same license:</strong> Use the same license for derivatives</li>
</ul>

<h4>❌ Limitations</h4>
<ul>
  <li>No liability</li>
  <li>No warranty</li>
</ul>
</div>

<h2>🤝 Contributing</h2>
<div align="center">
  <table>
    <tr>
      <td>🍴 Fork</td>
      <td>🔧 Code</td>
      <td>📤 Push</td>
      <td>📫 PR</td>
    </tr>
  </table>
</div>

<h2>📞 Support</h2>
<div align="center">
  <p>If this project helps you, please consider:</p>
  <p>
    ⭐ Giving it a star<br>
    🐛 Reporting issues<br>
    💡 Suggesting improvements<br>
    🤝 Contributing code
  </p>
</div>

---

<div align="center" style="margin-top: 50px;">
  <p style="color: #666; font-style: italic;">Made with ❤️</p>
  
  <a href="https://github.com/desiFish/ESP32-GPS-CLOCK-V1/stargazers">
    <img src="https://img.shields.io/github/stars/desiFish/ESP32-GPS-CLOCK-V1?style=social" alt="Stars">
  </a>
</div>
