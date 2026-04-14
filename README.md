# ircserv 📡

> irc server from scratch. c++98. no boost. no mercy.

a fully functional irc server built in c++98. connects with any real irc client, handles channels, operators, modes, and everything else you'd expect from a chat server built in 1988.

---

## supported commands

| command | what it does |
|---|---|
| `PASS` | authenticate before registering |
| `NICK` | set or change nickname |
| `USER` | register username and realname |
| `JOIN` | join or create a channel |
| `PART` | leave a channel |
| `PRIVMSG` | send a message to a user or channel |
| `KICK` | remove someone from a channel (op only) |
| `INVITE` | invite someone to an invite-only channel |
| `TOPIC` | view or set the channel topic |
| `MODE` | set channel or user modes |
| `QUIT` | disconnect from the server |
| `PING/PONG` | keep-alive |
| `WHO/WHOIS` | look up users |
| `LIST/NAMES` | list channels and their members |
| `MOTD` | message of the day |

---

## channel modes

| mode | flag | description |
|---|---|---|
| invite-only | `+i` | only invited users can join |
| topic lock | `+t` | only operators can change topic |
| channel key | `+k` | password-protected channel |
| user limit | `+l` | max number of users |
| operator | `+o` | grant/revoke operator privileges |

---

## architecture

```
main.cpp
├── Server          ← event loop (poll), client/channel management
├── ServerCommands  ← all IRC command handlers
├── Client          ← per-connection state, buffer, registration
└── Channel         ← members, operators, modes, broadcast
```

non-blocking i/o with `poll()`. one loop, everything goes through it.

---

## build & run

```bash
make
./ircserv <port> <password>

# example
./ircserv 6667 mypassword
```

then connect with any irc client:

```bash
# irssi
irssi -c localhost -p 6667 -w mypassword

# weechat
/server add local localhost/6667 -password=mypassword
/connect local
```

---

## commands

```bash
make        # build
make clean  # remove objects
make fclean # remove objects + binary
make re     # fclean + make
```

---

## project structure

```
.
├── Makefile
├── main.cpp
├── Server.cpp / Server.hpp
├── ServerCommands.cpp
├── Client.cpp / Client.hpp
└── Channel.cpp / Channel.hpp
```

---

made at **1337 benguerir** · 42 network  
`wel-kass` · [intra](https://profile.intra.42.fr/users/wel-kass)
