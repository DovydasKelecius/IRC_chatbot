IRC chatbot for unix2025task3

✨ Features
Gemini AI Integration: Connects to Google's Gemini API for advanced conversational capabilities.
Multi-Process Architecture: Dedicated child processes (workers) for each IRC channel prevent blocking and ensure that AI responses are processed concurrently.
Pinger Process: A separate child process keeps the IRC connection alive by periodically sending PINGs, ensuring stability.
Robust Inter-Process Communication (IPC): Utilizes POSIX pipes for parent-to-child communication and semaphores for safe, synchronized access to the IRC socket.
Configurable Channels: Easily define channels, their associated AI personas, and whether AI features are enabled via a simple configuration file.
Mute Functionality: Admins can mute specific users to prevent the bot from responding to them. (From admin channel)
Dynamic API Key Loading: Loads the Gemini API key securely from environment variables.
Error Logging: Comprehensive logging provides insights into bot operations, warnings, and errors.
Graceful Shutdown: Handles SIGINT and SIGTERM signals for clean shutdown of all child processes and resource deallocation.

🛠️ Technologies Used

- C Language: The core of the application.
- libcurl: For making HTTP requests to the Gemini API.
- cJSON: For parsing and generating JSON data.
- POSIX Inter-Process Communication (IPC):
- fork(): For creating child processes.
- pipe(): For one-way communication between parent and worker children.
- semaphores: For synchronizing access to the shared IRC socket.
- Shared memory: For locking the socket when each child or parent sends their wanted information.
- Signals: For receiving the Ctrl+C shutdown sequence. And overriding it with a graceful shutdown.
- select(): For non-blocking I/O on pipes in worker processes.
- make: For building the project.


Prerequisites
- A C compiler (e.g., GCC)
- make
- libcurl development libraries (libcurl-dev or libcurl-devel depending on your distribution).
- A Google Gemini API Key. You can obtain one from Google AI Studio.
Example Installation (Ubuntu/Debian)
Bash

sudo apt update
sudo apt install build-essential libcurl4-openssl-dev
1. Clone the Repository
Bash

git clone https://git.mif.vu.lt/doke1272/unix25task3.git
cd unix25task3

2. Configure Your Channels and AI Personas
Create a file named channels.txt in the project root with the following format:

- #admin_channel; <-ALWAYS MUST BE FIRST
- #channel1; persona_description_for_channel1_here
- #channel2; another_persona_for_channel2

3. Set Your Gemini API Key
The bot reads the Gemini API key from an environment variable.
Never hardcode your API key directly in the code.

Bash

export GEMINIAI_API_KEY="YOUR_ACTUAL_GEMINI_API_KEY_HERE"

You can add this line to your ~/.bashrc or ~/.zshrc file to set it automatically every time you open a new terminal session.

4. Build the Bot
From the project root directory, run make:

Bash

make

This will compile the source files and create the irc_chatbot executable.

5. Run the Bot
Bash

./irc_chatbot [IP_address] [PORT]

The bot will connect to the IRC server, join the specified channels, and its worker processes will be ready to respond.

-------------------------------------------------------------------------

Bot Commands (in IRC)
These commands are processed by the bot's parent or worker processes based on the channel.

- !ask <your_question>: Ask the AI a question in a channel where AI features are enabled. The bot will respond based on the channel's configured persona.

Example: !ask What's the capital of France?
- !hello: A simple test command to check if the worker is active. The bot will send a "Hello!" response.

Admin Commands (in the Admin Channel):

- !mute [nickname]: Mute a user, preventing the bot from responding to their !ask commands.
- !unmute [nickname]: Unmute a previously muted user.
- !status : Will give a list of active children and their specific status / channel they reside.
- !users : Will give a list of users currently joined that have joined your created (or specified) channels.

-------------------------------------------------------------------------

Shutting Down

To gracefully shut down the bot and all its child processes, simply press Ctrl+C in the terminal where the bot is running. The bot is configured to handle SIGINT (Ctrl+C) and SIGTERM signals, ensuring all resources are properly freed.
