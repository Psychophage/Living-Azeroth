# Setup

## What you need

- Linux (x86-64) with [Docker Engine](https://docs.docker.com/engine/install/) and its
  Compose plugin, and Python 3.11 or newer.
- About 25 GB of free disk space and 16 GB of RAM (32 GB is comfortable with 500 bots).
- A World of Warcraft **3.3.5a (build 12340)** client. It is not included.
- Optional, for conversations: an [OpenRouter](https://openrouter.ai/) API key.
  Everything else works without one.

## Set up a realm

```bash
git clone https://github.com/Psychophage/Living-Azeroth.git
cd Living-Azeroth
./living-azeroth setup --realm ~/living-azeroth-realm
```

Setup asks for a realm name, your API key (Enter skips it) and an administrator
account, then:

1. downloads the server's map data (about 1.5 GB, once per computer);
2. builds the toolchain image and compiles the server (the first build takes a while);
3. creates the databases, the spending budget and your administrator account.

Then start the realm:

```bash
./living-azeroth start
./living-azeroth logs      # wait for "ready...", then Ctrl+C (the realm keeps running)
```

## Connect a client

In the client's `Data/enUS/realmlist.wtf`, set `set realmlist 127.0.0.1` and log in
with the administrator account. Friends get their own accounts:
`./living-azeroth account their-name`.

To let others on your network play, set `bind = 0.0.0.0` and `address` to this
computer's network address in the realm's `realm.conf`, then
`./living-azeroth restart`. See [Configuration](configuration.md).

## Several realms

Each realm is its own folder with its own database, ports and settings. Pass
`--realm FOLDER` to any command; without it, commands use the first realm you set
up. Give a second realm different ports: `setup --realm FOLDER --auth-port 13724
--world-port 18085`.
