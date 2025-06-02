#ifndef GEMINI_INTEGRATION_H
#define GEMINI_INTEGRATION_H

#include <stddef.h> // For size_t

// Function to get a response from the Gemini API
// persona: A system message to set the AI's role (optional, can be NULL)
// user_prompt: The user's query
// api_key: Your Google Gemini API key
// Returns a dynamically allocated string containing the AI's response, or NULL on error.
// The caller is responsible for freeing the returned string.
char* get_gemini_response(const char* persona, const char* user_prompt, const char* api_key);

#endif // GEMINI_INTEGRATION_H