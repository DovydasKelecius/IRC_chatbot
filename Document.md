Started with connection to the IRC channel using given examples. Modifying it for personal use.

Wrote a simple CTRL + C signal handler, where it is required for at least three presses for the Bot to disconnect. Still haven't done proper disconnection.

[Challenge] Create a proper Ping Pong system so the Bot isn't kicked after N amount of seconds. Done

After a few days of trial and error I've reconstructed the code into individual functions as I was getting confused and distracted.

For now I just decided that the shared memory will be used for checking if the global socket is available to send messages with. Haven't tested with receiving them.

[Challenge] Connect each bot to a listed available channel. So far I've only managed to put one inside. Done

For now I'm trying to connect to the server and wait and see if the Pinger child PINGs. 

Decided to use mmap as from researching forums basically understood that it is easier to use but limiting. Since I didn't need a complex shared memory I decided on mmap.

Using the first child as a PINGER for  the server, while all the other ones will be used to maintain the channels.

Each channel has designated child which can only SEND, while the parent/root will mainly RECEIVE, with some exceptions (mainly admin channel).

Created an admin channel that can only be first in the channels.txt list, it has a password that only people with the key have (i count them as admins. I know this is not that secure)

All  the other listed  channels will count as worker channels with their respective bots and channel names and uses. I have no idea what my bot will do so I just did some of the points from  the given task and the rest will probably be filled with just a AI bot.

Also my space is broken somewhat and sometimes it just clicks two times...

I've divided the bigger parts that are  distinct into  their own files, this makes it a bit difficult to navigate but it's probably more  professional?

Made a makefile to  easily  clean all of  the aftermath of compiling and clean compiling as well with options.

Created a muted users list where it will automatically update and be read as it is  dynamically used. Added !mute <user>, !unmute <user> commands.

Redid the logging system to read easier and timestamps, everything  is appended to the log file. Basically everything that bot  receives, sends and logs  is there.

I've been trying  to connect to 10.1.0.46 6667 but for 4  days couldn't on personal pc, linux2 and linux1 machine by connected  VPN and VU opennebula VM so either im doing something wrong or  it's offline... I hope it's not a requirement for the bot to be there.

Did a lot of logging in case of fails. Some lethal some not. 

Been having trouble with Users list, I've changed something in the  code and it stopped  sending the user list  for each channel. Haven't found a way to fix it yet.

Intergrated Gemini AI into the bots, to  use them the client needs to export an env to your system. Or  it just passes it if not found and !ask will  not work.

The AI is very "smart"  so it basically  forgets the description I gave  it in 4 sentences. Not expecting much but at least it has funny first few lines.

I've updated  README document to show installation and use for this bot. 

Cleaned up some comments and code, still havent found if !users work. My testing users for some reason disconnect on any irc server :(

The project is I  think (?). Does what it requires (i hope the shared  memory part is enough for one variable boolean). I know that the socket itself is in shared memory by default  but im not  sure  about the locking mechanism. So I'm betting that it works!

There were many more challenges but It was mainly debugging why something wasnt detected.