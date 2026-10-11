#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <string.h>

#include "WebServer.hpp"
#include "../Hardware/LightDriver.hpp"

#include "Api.hpp"

#define LIGHTS_NUM_MOD    11

// Singleton instance of CApi class, available everywhere
CApi Api;

// API endpoint handler for "/SPI/light"
static esp_err_t ApiEndpointLight(httpd_req_t *req, const char* path, const char* query, const char* fragment)
{
    esp_err_t err = ESP_OK;
    char* postBuff = nullptr;
    cJSON* root = nullptr;
    cJSON* outputsArray = nullptr;
    uint8_t* outputStates = nullptr;
    int outputIndex;
    cJSON *outputElement;

    // Limit request size
    size_t content_len = req->content_len;
    if (content_len <= 0 || content_len > 8192) {
        // Request too long
        err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content length too long");
        if (err != ESP_OK)
            return err;
        return ESP_FAIL;
    }

    // Allocate buffer to receive entire POST
    postBuff = (char*)malloc(content_len + 1); // +1 for null terminator
    if (!postBuff) {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        if (err != ESP_OK)
            return err;
        return ESP_FAIL;
    }

    // Read the entire POST request
    int ret = httpd_req_recv(req, postBuff, content_len);
    if (ret <= 0) {
        // Read failed
        // Free the read buffer
        free(postBuff);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            // Client did not send entire POST body in a timely manner
            err = httpd_resp_send_408(req); // HTTP timeout status code
            if (err != ESP_OK)
                return err;
        }
        return ESP_FAIL;
    }
    postBuff[content_len] = '\0'; // Ensure null-termination    

    // Parse out JSON payload
    root = cJSON_Parse(postBuff);

    // Free the POST buffer here to save memory for later
    // Can do this right after cJSON_Parse() because it allocates its own storage buffers
    free(postBuff);
    postBuff = nullptr;

    // Make sure JSON parsed correctly
    if (!root) {
        // Parse failure
        ESP_LOGE(CApi::TAG, "Failed to parse JSON structure");
        err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid schema");
        if (err == ESP_OK) err = ESP_FAIL; // Set failure
        goto lightExit;
    }

    // Get the "outputs" array
    // We can declare this here because we only need to delete the root, not any objects retrieved from it
    outputsArray = cJSON_GetObjectItem(root, "outputs");
    if (outputsArray == NULL || !cJSON_IsArray(outputsArray)) {
        // "outputs" array not found
        ESP_LOGE(CApi::TAG, "Key 'outputs' not found or is not an array");
        err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid schema");
        if (err == ESP_OK) err = ESP_FAIL; // Set failure
        goto lightExit;
    }

    // Validate the array size is correct
    if (cJSON_GetArraySize(outputsArray) != LIGHTS_NUM_MOD) {
        // Array size is not correct
        ESP_LOGE(CApi::TAG, "Key 'outputs' has incorrect array size");
        err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incorrect number of outputs");
        if (err == ESP_OK) err = ESP_FAIL; // Set failure
        goto lightExit;
    }

    // Allocate memory for the output state buffer
    outputStates = (uint8_t*)malloc(LIGHTS_NUM_MOD);

    // Parse out the output state
    outputIndex = 0;
    outputElement = nullptr;
    cJSON_ArrayForEach(outputElement, outputsArray) {
        // Validate array element is acceptable
        if (!cJSON_IsNumber(outputElement) || outputElement->valueint < 0x00 || outputElement->valueint > 0xFF) {
            ESP_LOGE(CApi::TAG, "Output index %d is invalid", outputIndex);
            err = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid values for outputs");
            if (err == ESP_OK) err = ESP_FAIL; // Set failure
            goto lightExit;
        }

        // Save the required output state at current index
        outputStates[outputIndex] = (uint8_t)outputElement->valueint;

        // On to next output
        outputIndex++;
    }

    // Send the updated state to the driver
    LightDriver.Update(outputStates);

    // Set MIME type for response
    err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) goto lightExit;

    // Send response
    err = httpd_resp_send(req, "{\"status\":\"OK\"}", HTTPD_RESP_USE_STRLEN);

lightExit:
    // Go to here if need to abort on an error

    // Free output state buffer, if allocated
    if (outputStates) {
        free(outputStates);
        outputStates = nullptr;
    }

    // Free JSON structures, if allocated
    if (root) {
        cJSON_Delete(root);
        root = nullptr;
    }

    // Free POST receival buffer, if allocated
    if (postBuff) {
        free(postBuff);
        postBuff = nullptr;
    }

    return err;

}

esp_err_t CApi::Initialize() {
    esp_err_t err;

    // Register API endpoint
    err = WebServer.RegisterAPIEndpoint("/API/lights", HTTP_POST, ApiEndpointLight);
    if (err != ESP_OK) return err;

    return ESP_OK;
}
