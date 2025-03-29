# idfnode

**Template project for ESP-IDF (Espressif IoT Development Framework)**  
Minimal, extensible ESP32 firmware boilerplate – ready for real-world IoT projects.

---

## 📦 Included Modules

This template project includes the following components:

### ✅ Wi-Fi Setup
Connects ESP32 to your Wi-Fi network using either `sdkconfig` or hardcoded credentials.  
You can configure Wi-Fi via `idf.py menuconfig` or by editing the code directly.

### ✅ OTA (Over-the-Air Updates)
Built-in HTTP OTA server on ESP32 – accepts firmware uploads via HTTP POST.  
Firmware can be uploaded using the included `ota.py` Python client.

### ✅ UDP Logger
ESP32 sends all logs (`ESP_LOGx`) over UDP to the first client that sends a "ping" packet.  
No IP configuration needed – works automatically in a local network.

### ✅ Git Clean Setup
Includes `.gitignore` and `sdkconfig.defaults` to keep your repository clean and portable.

---

## 🚀 Getting Started

### 1. Clone the Repository

```bash
git clone git@github.com:pwilga/idfnode.git
cd idfnode
