#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/md5.h"

#define RETURN_IF_FALSE(x)                                                     \
  do {                                                                         \
    if (!(x))                                                                  \
      return;                                                                  \
  } while (0)

#define RX_BUFFER_SIZE 1024
#define MAGIC_BYTES 0xAF, 0xCA, 0xEC, 0x2D, 0xFE, 0x55
#define ACK_CODE 0xAA, 0x55

#define MD5_SIZE 16
#define SW_VERSION_SIZE 2

static const char *TAG = "ota-update";

static const char *ota_steps[] = {
    "Waiting for OTA magic header",
    "Receiving firmware metadata (version, size, MD5)",
    "Initializing OTA update",
    "Writing firmware",
    "Verifying firmware",
    "Rebooting"};

static const size_t ota_steps_count = sizeof(ota_steps) / sizeof(ota_steps[0]);

#define OTA_LOG_STEP(n)                                                        \
  if ((n) < ota_steps_count)                                                   \
  ESP_LOGI(TAG, "[%d/%d] %s...", (n) + 1, ota_steps_count, ota_steps[n])

static bool send_ack(const int client_sock) {
  const uint8_t ack_code[] = {ACK_CODE};

  bool result =
      send(client_sock, ack_code, sizeof(ack_code), 0) == sizeof(ack_code);

  if (!result)
    ESP_LOGE(TAG, "Failed to send ACK: errno %d", errno);
  // else
  //     ESP_LOGI(TAG, "ACK sent successfully");

  return result;
}

int read_all(const int client_sock, uint8_t *rx_buffer) {
  int len = recv(client_sock, rx_buffer, RX_BUFFER_SIZE, 0);

  if (len < 0)
    ESP_LOGE(TAG, "Receiving data failed: errno %d", errno);
  else if (!len)
    ESP_LOGW(TAG, "Connection closed");

  return len <= 0 ? 0 : len;
}

static void handle_ota(const int client_sock) {
  uint32_t firmware_size = 0;
  uint8_t sw_version[SW_VERSION_SIZE];
  uint8_t md5_expected[MD5_SIZE];

  uint8_t rx_buffer[RX_BUFFER_SIZE];
  const uint8_t magic_bytes[] = {MAGIC_BYTES};

  OTA_LOG_STEP(0); // Magic header
  if (read_all(client_sock, rx_buffer) == sizeof(magic_bytes)) {
    if (memcmp(rx_buffer, magic_bytes, sizeof(magic_bytes))) {
      ESP_LOGE(TAG, "Invalid OTA magic header");
      return;
    }
    RETURN_IF_FALSE(send_ack(client_sock));
  }

  OTA_LOG_STEP(1); // Version, Size, MD5
  if (read_all(client_sock, rx_buffer) == sizeof(sw_version)) {
    memcpy(sw_version, rx_buffer, sizeof(sw_version));
    RETURN_IF_FALSE(send_ack(client_sock));
  }

  if (read_all(client_sock, rx_buffer) == sizeof(uint32_t)) {
    firmware_size = ((uint32_t)rx_buffer[0] << 24) |
                    ((uint32_t)rx_buffer[1] << 16) |
                    ((uint32_t)rx_buffer[2] << 8) | ((uint32_t)rx_buffer[3]);

    RETURN_IF_FALSE(send_ack(client_sock));
  }

  if (read_all(client_sock, rx_buffer) == sizeof(md5_expected)) {
    memcpy(md5_expected, rx_buffer, sizeof(md5_expected));
    RETURN_IF_FALSE(send_ack(client_sock));
  }

  OTA_LOG_STEP(2); // OTA start

  esp_ota_handle_t ota_handle = 0;
  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);

  esp_err_t err = esp_ota_begin(update_partition, firmware_size, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to begin OTA update: %s", esp_err_to_name(err));
    return;
  }

  // Initialize MD5 context
  mbedtls_md5_context md5_ctx;
  mbedtls_md5_init(&md5_ctx);
  mbedtls_md5_starts(&md5_ctx);

  // Ready for image transmission

  OTA_LOG_STEP(3); // Write

  uint32_t total_read_bytes = 0;
  uint16_t read_bytes = -1;
  uint8_t last_percent_reported = -1;

  while (read_bytes) {
    read_bytes = read_all(client_sock, rx_buffer);
    total_read_bytes += read_bytes;

    int percent = (100 * total_read_bytes) / firmware_size;
    if (percent / 10 != last_percent_reported / 10) {
      ESP_LOGI(TAG, "Progress: %d%% (%d/%d bytes)", percent, total_read_bytes,
               firmware_size);
      last_percent_reported = percent;
    }

    err = esp_ota_write(ota_handle, rx_buffer, read_bytes);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to write OTA data chunk (%d bytes): %s", read_bytes,
               esp_err_to_name(err));
      esp_ota_end(ota_handle);
      return;
    }
    mbedtls_md5_update(&md5_ctx, rx_buffer, read_bytes);
  }

  RETURN_IF_FALSE(send_ack(client_sock));

  OTA_LOG_STEP(4); // Verification
  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Finalizing OTA update failed: %s", esp_err_to_name(err));
    return;
  }

  // Finalize MD5 calculation
  uint8_t md5_calc[MD5_SIZE];
  mbedtls_md5_finish(&md5_ctx, md5_calc);
  mbedtls_md5_free(&md5_ctx);

  // Compare calculated MD5 with expected
  if (memcmp(md5_calc, md5_expected, sizeof(md5_expected))) {
    ESP_LOGE(TAG, "MD5 mismatch!");
    return;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Setting new boot partition failed: %s",
             esp_err_to_name(err));
    return;
  }

  OTA_LOG_STEP(5); // Reboot
  esp_safe_restart(NULL);
}

void tcp_ota_task(void *args) {

  struct sockaddr_storage dest_addr;

  struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
  dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr_ip4->sin_family = AF_INET;
  dest_addr_ip4->sin_port = htons(CONFIG_OTA_TCP_PORT);

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_sock < 0) {
    ESP_LOGE(TAG, "Unable to create socket. System error code: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  /*
      SO_REUSEADDR option allows to reuse the same IP address and port even
      if they were recently used and are still in the TIME_WAIT state.
  */
  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) ||
      listen(listen_sock, 1)) {
    ESP_LOGE(TAG, "Socket setup failed. Step: %s, Error code: %d",
             (errno == EADDRINUSE || errno == EADDRNOTAVAIL) ? "bind"
                                                             : "listen",
             errno);
    close(listen_sock);
    vTaskDelete(NULL);
  }

  while (1) {
    ESP_LOGI(TAG, "Listening for connections, port:%d.", CONFIG_OTA_TCP_PORT);

    struct sockaddr_storage source_addr;
    socklen_t addr_len = sizeof(source_addr);
    int client_sock =
        accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (client_sock < 0) {
      ESP_LOGE(TAG, "Error accepting incoming connection: errno %d", errno);
      break;
    }

    char addr_str[16];
    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str,
                sizeof(addr_str) - 1);
    ESP_LOGI(TAG, "Incoming connection from %s", addr_str);

    handle_ota(client_sock);

    shutdown(client_sock, 0);
    close(client_sock);
  }
}