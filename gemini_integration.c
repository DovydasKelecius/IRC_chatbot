#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "irc_bot.h"
#include "gemini_integration.h" 
#include "cJSON.h" 

// --- Constants for Gemini API ---
#define GEMINI_MODEL "gemini-1.5-flash"
#define GEMINI_API_ENDPOINT "https://generativelanguage.googleapis.com/v1beta/models/" GEMINI_MODEL ":generateContent"

// Structure to hold response data from libcurl
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Callback function for libcurl to write received data into memory
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    app_log("Gemini_API", "ERROR", "not enough memory (realloc returned NULL)");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0; // Null-terminate the buffer

  return realsize;
}

char* get_gemini_response(const char* persona, const char* user_prompt, const char* api_key) {
    if (api_key == NULL || strlen(api_key) == 0) {
        app_log("Gemini_API", "ERROR", "API key is not set. Cannot make request.");
        return NULL;
    }
    if (user_prompt == NULL || strlen(user_prompt) == 0) {
        app_log("Gemini_API", "ERROR", "User prompt is empty. Cannot make request.");
        return NULL;
    }

    CURL *curl_handle = NULL;
    CURLcode res;
    struct MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    struct curl_slist *headers = NULL;
    cJSON *json_root = NULL;
    char *json_payload = NULL;
    char *response_text = NULL;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        app_log("Gemini_API", "ERROR", "curl_easy_init() failed.");
        goto cleanup;
    }

    // Construct JSON payload for Gemini API
    json_root = cJSON_CreateObject();
    if (json_root == NULL) {
        app_log("Gemini_API", "ERROR", "Failed to create JSON object for request.");
        goto cleanup;
    }

    cJSON *contents_array = cJSON_CreateArray();
    if (contents_array == NULL) {
        app_log("Gemini_API", "ERROR", "Failed to create JSON contents array.");
        goto cleanup;
    }
    cJSON_AddItemToObject(json_root, "contents", contents_array);

    // Add persona/system message (as a 'user' role for prompt instruction)
    if (persona && strlen(persona) > 0) {
        cJSON *persona_message_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(persona_message_obj, "role", "user");
        cJSON *persona_parts_array = cJSON_CreateArray();
        cJSON *persona_text_part = cJSON_CreateObject();
        cJSON_AddStringToObject(persona_text_part, "text", persona);
        cJSON_AddItemToArray(persona_parts_array, persona_text_part);
        cJSON_AddItemToObject(persona_message_obj, "parts", persona_parts_array);
        cJSON_AddItemToArray(contents_array, persona_message_obj);
    }

    // Add user message (prompt)
    cJSON *user_message_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(user_message_obj, "role", "user");
    cJSON *user_parts_array = cJSON_CreateArray();
    cJSON *user_text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(user_text_part, "text", user_prompt);
    cJSON_AddItemToArray(user_parts_array, user_text_part);
    cJSON_AddItemToObject(user_message_obj, "parts", user_parts_array);
    cJSON_AddItemToArray(contents_array, user_message_obj);

    // Add optional safety settings
    cJSON *safety_settings_array = cJSON_CreateArray();
    if (safety_settings_array == NULL) {
        app_log("Gemini_API", "ERROR", "Failed to create JSON safety settings array.");
        goto cleanup;
    }
    const char *categories[] = {
        "HARM_CATEGORY_HARASSMENT",
        "HARM_CATEGORY_HATE_SPEECH",
        "HARM_CATEGORY_SEXUALLY_EXPLICIT",
        "HARM_CATEGORY_DANGEROUS_CONTENT"
    };
    for (size_t i = 0; i < sizeof(categories) / sizeof(categories[0]); ++i) {
        cJSON *setting_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(setting_obj, "category", categories[i]);
        cJSON_AddStringToObject(setting_obj, "threshold", "BLOCK_NONE");
        cJSON_AddItemToArray(safety_settings_array, setting_obj);
    }
    cJSON_AddItemToObject(json_root, "safetySettings", safety_settings_array);

    // Add optional generation configuration
    cJSON *generation_config_obj = cJSON_CreateObject();
    if (generation_config_obj == NULL) {
        app_log("Gemini_API", "ERROR", "Failed to create JSON generation config object.");
        goto cleanup;
    }
    cJSON_AddNumberToObject(generation_config_obj, "temperature", 0.7); 
    cJSON_AddNumberToObject(generation_config_obj, "maxOutputTokens", 250);
    cJSON_AddItemToObject(json_root, "generationConfig", generation_config_obj);


    json_payload = cJSON_PrintUnformatted(json_root);
    if (json_payload == NULL) {
        app_log("Gemini_API", "ERROR", "Failed to print JSON payload to string.");
        goto cleanup;
    }
    app_log("Gemini_API", "DEBUG", "Request Payload: %s", json_payload);

    // Set libcurl options
    char full_url[512]; // Adjust size as needed
    snprintf(full_url, sizeof(full_url), "%s?key=%s", GEMINI_API_ENDPOINT, api_key);

    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl_handle, CURLOPT_URL, full_url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "irc-bot-gemini/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L); // 30 seconds timeout
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);

    // Perform the request
    app_log("Gemini_API", "INFO", "Sending request to Gemini...");
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        app_log("Gemini_API", "ERROR", "curl_easy_perform() failed: %s", curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
        app_log("Gemini_API", "INFO", "Received HTTP response code: %ld", http_code);
        app_log("Gemini_API", "DEBUG", "Response Data: %s", chunk.memory ? chunk.memory : "N/A");

        if (http_code == 200 && chunk.memory) {
            cJSON *json_response = cJSON_Parse(chunk.memory);
            if (json_response == NULL) {
                const char *error_ptr = cJSON_GetErrorPtr();
                if (error_ptr != NULL) {
                    app_log("Gemini_API", "ERROR", "Failed to parse JSON response: %s (Raw: %s)", error_ptr, chunk.memory);
                } else {
                    app_log("Gemini_API", "ERROR", "Failed to parse JSON response (unknown error). Raw: %s", chunk.memory);
                }
            } else {
                // --- Parse Gemini specific response structure ---
                cJSON *candidates = cJSON_GetObjectItemCaseSensitive(json_response, "candidates");
                if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
                    cJSON *first_candidate = cJSON_GetArrayItem(candidates, 0);
                    cJSON *content = cJSON_GetObjectItemCaseSensitive(first_candidate, "content");
                    if (content) {
                        cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
                        if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                            cJSON *first_part = cJSON_GetArrayItem(parts, 0);
                            cJSON *text_content = cJSON_GetObjectItemCaseSensitive(first_part, "text");
                            if (cJSON_IsString(text_content) && (text_content->valuestring != NULL)) {
                                response_text = strdup(text_content->valuestring);
                                app_log("Gemini_API", "INFO", "Successfully extracted AI response.");
                            } else {
                                app_log("Gemini_API", "WARN", "Could not find 'text' string in Gemini response content part.");
                            }
                        } else {
                            app_log("Gemini_API", "WARN", "No 'parts' array in Gemini response content or it's empty.");
                        }
                    } else {
                        app_log("Gemini_API", "WARN", "No 'content' object in Gemini response candidate.");
                    }
                } else {
                    app_log("Gemini_API", "WARN", "No 'candidates' array in Gemini response or it's empty.");
                    // Check for error object if no candidates
                    cJSON *error_obj = cJSON_GetObjectItemCaseSensitive(json_response, "error");
                    if (error_obj) {
                        cJSON *error_message = cJSON_GetObjectItemCaseSensitive(error_obj, "message");
                        if (cJSON_IsString(error_message)) {
                            app_log("Gemini_API", "ERROR", "Gemini API returned error: %s", error_message->valuestring);
                        } else {
                            app_log("Gemini_API", "ERROR", "Gemini API returned error without a clear message.");
                        }
                    }
                }
                cJSON_Delete(json_response);
            }
        } else {
             app_log("Gemini_API", "ERROR", "Gemini API request failed with HTTP code %ld. Response: %s", http_code, chunk.memory ? chunk.memory : "N/A");
        }
    }

cleanup:
    if (curl_handle) curl_easy_cleanup(curl_handle);
    if (headers) curl_slist_free_all(headers);
    if (json_payload) free(json_payload);
    if (json_root) cJSON_Delete(json_root);
    if (chunk.memory) free(chunk.memory);
    curl_global_cleanup();

    return response_text;
}