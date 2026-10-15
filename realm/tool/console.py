"""The realm console: a live view of one realm, with its everyday commands one key away.

Each key runs the same `living-azeroth` command you would type, and shows it. Closing the
console leaves the realm as it is.
"""

import datetime
import json
import os
from pathlib import Path
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import termios
import threading
import time
import tty

try:
    from rich import box
    from rich.console import Console, Group
    from rich.live import Live
    from rich.panel import Panel
    from rich.table import Table
    from rich.text import Text
except ImportError:  # the realm tools themselves need nothing beyond Python
    raise SystemExit("The console needs the Python package rich: install python-rich from your system's "
                     "packages, or run `python3 -m pip install --user rich`.")

from config import LEDGER_SCHEMA, SOURCE, database_host, read_settings, read_state, shared_budget

CLI = SOURCE / "living-azeroth"
GOLD, DIM, GOOD, BUSY, BAD = "#e0b354", "#8a8f9c", "#5fd38d", "#e8b04a", "#e5675c"
CLASSES = {1: "Warrior", 2: "Paladin", 3: "Hunter", 4: "Rogue", 5: "Priest", 6: "Death Knight", 7: "Shaman",
           8: "Mage", 9: "Warlock", 11: "Druid"}
# Server log lines that are noise in a summary: the periodic timing report.
LOG_NOISE = re.compile(r"^(\|-|Last \d+ diffs|Update time diff|\s*$)")
TICK = re.compile(r"Update time diff: (\d+)ms with (\d+) players online")
REFRESH_SECONDS = 2.0


# ---------------------------------------------------------------- reading the realm

def zone_names(client_data):
    """Zone and area names from the server's own map data (AreaTable.dbc)."""
    path = Path(client_data) / "dbc/AreaTable.dbc"
    try:
        data = path.read_bytes()
        _, count, fields, size, _ = struct.unpack("<4s4I", data[:20])
        strings = data[20 + count * size:]
        names = {}
        for index in range(count):
            row = struct.unpack_from(f"<{fields}I", data, 20 + index * size)
            names[row[0]] = strings[row[11]:strings.index(b"\0", row[11])].decode("utf-8", "replace")
        return names
    except (OSError, ValueError, struct.error):
        return {}


def query(host, sql):
    """Rows of a read-only query, or None when the database cannot be reached."""
    command = ["docker", "exec", host, "sh", "-c",
               'MYSQL_PWD="$(cat /run/secrets/database-password)" mysql -uroot -N -B -e "$0"', sql]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return None
    if result.returncode != 0:
        return None
    return [line.split("\t") for line in result.stdout.splitlines()]


def containers(project):
    """Each service's state: (status, started, exit code), from Docker."""
    names = {service: f"{project}-{service}-1" for service in ("database", "auth", "world")}
    result = subprocess.run(["docker", "inspect", *names.values()], capture_output=True, text=True)
    found = {}
    try:
        for item in json.loads(result.stdout or "[]"):
            found[item["Name"].lstrip("/")] = item["State"]
    except json.JSONDecodeError:
        pass
    states = {}
    for service, name in names.items():
        state = found.get(name)
        if not state:
            states[service] = ("absent", None, None)
            continue
        started = None
        if state.get("StartedAt", "").startswith("2"):
            started = datetime.datetime.fromisoformat(state["StartedAt"][:19]).replace(tzinfo=datetime.timezone.utc)
        states[service] = (state.get("Status", "unknown"), started, state.get("ExitCode"))
    return states


def tail(path, lines=400):
    try:
        with open(path, "rb") as log:
            log.seek(0, os.SEEK_END)
            log.seek(max(0, log.tell() - 64 * 1024))
            return log.read().decode("utf-8", "replace").splitlines()[-lines:]
    except OSError:
        return []


class Snapshot:
    """Everything the console shows, read once per refresh."""

    def __init__(self, realm, names):
        state = read_state(realm)
        settings = read_settings(realm)["realm"]
        self.name = settings.get("name", realm.name)
        self.folder = realm.name
        self.address = f"{settings.get('address', '127.0.0.1')}:{settings.get('auth_port', '3724')}"
        self.services = containers(state["project"])
        world, started, code = self.services["world"]
        log = tail(realm / "logs/Server.log")
        self.ready = world == "running" and any("ready..." in line for line in log)
        self.started = started
        self.crashed = world == "exited" and code not in (0, None)
        self.exit_code = code
        ticks = [TICK.search(line) for line in log]
        ticks = [tick for tick in ticks if tick]
        self.tick = int(ticks[-1].group(1)) if ticks and self.ready else None
        self.events = [line for line in log if not LOG_NOISE.match(line)][-200:]
        self.players, self.bots, self.roster, self.spending = [], None, None, None
        self.unreadable = False  # the database is up but a query failed: say so rather than show nobody
        if self.services["database"][0] != "running":
            return
        host = database_host(state)
        rows = query(host,
                     "SELECT a.username, c.name, c.level, c.class, c.zone FROM acore_characters.characters c "
                     "JOIN acore_auth.account a ON a.id = c.account WHERE c.online = 1 AND c.account NOT IN "
                     "(SELECT account_id FROM acore_playerbots.playerbots_account_type) ORDER BY a.username, c.name")
        self.unreadable = rows is None
        if rows is not None:
            self.players = [(user, name, int(level), CLASSES.get(int(cls), "?"), names.get(int(zone), ""))
                            for user, name, level, cls, zone in rows]
        rows = query(host,
                     "SELECT SUM(c.online), COUNT(*) FROM acore_characters.characters c WHERE c.account IN "
                     "(SELECT account_id FROM acore_playerbots.playerbots_account_type)")
        if rows and rows[0][1] != "0":
            self.bots, self.roster = int(rows[0][0] or 0), int(rows[0][1])
        # The budget may live in another realm's database (shared_budget).
        owner_state = read_state(shared_budget(realm) or realm)
        budget = owner_state.get("budget_id")
        if budget and re.fullmatch(r"[A-Za-z0-9_.:-]+", budget):
            rows = query(database_host(owner_state),
                         f"SELECT ceiling_nano, spent_nano FROM {LEDGER_SCHEMA}.pbc_api_budget "
                         f"WHERE budget_id = '{budget}'")
            if rows:
                self.spending = (int(rows[0][1]) / 1e9, int(rows[0][0]) / 1e9)

    @property
    def status(self):
        if self.ready:
            return "Ready", GOOD
        if self.crashed:
            return f"Stopped unexpectedly (exit {self.exit_code})", BAD
        if self.services["world"][0] == "running":
            return "Starting: loading the world", BUSY
        if self.services["database"][0] == "running":
            return "Stopped (database still up)", DIM
        return "Stopped", DIM


# ---------------------------------------------------------------- drawing

ACTIONS = [
    ("s", "Start", "start"),
    ("x", "Stop", "stop"),
    ("r", "Restart", "restart"),
    ("u", "Update", "update"),
    ("l", "Server log", "logs"),
    ("b", "Back up", "backup"),
]


def since(moment):
    if not moment:
        return ""
    seconds = int((datetime.datetime.now(datetime.timezone.utc) - moment).total_seconds())
    hours, minutes = divmod(seconds // 60, 60)
    return f"{hours}h {minutes:02d}m" if hours else f"{minutes}m {seconds % 60:02d}s"


def dot(ok, busy=False):
    return Text("●", style=GOOD if ok else BUSY if busy else BAD)


def header(snap):
    label, colour = snap.status
    title = Text.assemble(("LIVING AZEROTH", f"bold {GOLD}"), ("   ", DIM), (f"{snap.folder} realm", "bold"))
    if snap.name != "Living Azeroth":
        title.append(f"  ·  {snap.name}", DIM)
    line = Text.assemble(("● ", colour), (label, f"bold {colour}"))
    if snap.ready:
        line.append(f"   up {since(snap.started)}", DIM)
        if snap.tick is not None:
            line.append(f"   ·   world tick {snap.tick} ms", DIM)
        line.append(f"   ·   log in at {snap.address}", DIM)
    grid = Table.grid(expand=True)
    grid.add_column()
    grid.add_column(justify="right")
    grid.add_row(title, Text(datetime.datetime.now().strftime("%H:%M:%S"), DIM))
    grid.add_row(line, "")
    return Panel(grid, box=box.HEAVY, border_style=GOLD, padding=(0, 1))


def services(snap):
    table = Table.grid(padding=(0, 1))
    table.add_column(width=1)
    table.add_column(style="bold")
    table.add_column(style=DIM)
    labels = {"database": "Database", "auth": "Login server", "world": "World server"}
    for service, label in labels.items():
        status, started, code = snap.services[service]
        if status == "running":
            ok = service != "world" or snap.ready
            detail = ("ready" if snap.ready else "loading the world") if service == "world" else "running"
            table.add_row(dot(ok, busy=not ok), label, detail)
        elif status == "absent":
            table.add_row(Text("○", DIM), label, "not created")
        else:
            failed = code not in (0, None)
            table.add_row(Text("●", BAD if failed else DIM), label, f"exited {code}" if failed else "stopped")
    return Panel(table, title="Servers", title_align="left", border_style=DIM, padding=(0, 1))


def world(snap):
    table = Table.grid(padding=(0, 2))
    table.add_column(style=DIM)
    table.add_column(style="bold")
    people = len(snap.players)
    table.add_row("Players", f"{people} online" if snap.services["database"][0] == "running" else "–")
    table.add_row("Bots", f"{snap.bots} online of {snap.roster:,}" if snap.bots is not None else "–")
    if snap.spending:
        spent, ceiling = snap.spending
        share = spent / ceiling if ceiling else 0
        colour = GOOD if share < 0.7 else BUSY if share < 0.9 else BAD
        bar = Text("━" * round(share * 16), style=colour) + Text("━" * (16 - round(share * 16)), style="#3a3f4b")
        table.add_row("Conversations", Text.assemble(f"${spent:.2f} of ${ceiling:.2f}   ", bar))
    else:
        table.add_row("Conversations", "–")
    return Panel(table, title="World", title_align="left", border_style=DIM, padding=(0, 1))


def players(snap):
    if not snap.players:
        note = "The database is stopped." if snap.services["database"][0] != "running" else \
            "The database could not be read." if snap.unreadable else "Nobody is logged in."
        return Panel(Text(note, DIM), title="Players online", title_align="left", border_style=DIM, padding=(0, 1))
    table = Table(box=box.SIMPLE_HEAD, expand=True, header_style=DIM, pad_edge=False, padding=(0, 1))
    for column, ratio in (("Account", 2), ("Character", 3), ("Level", 1), ("Class", 2), ("Zone", 4)):
        table.add_column(column, justify="right" if column == "Level" else "left", ratio=ratio)
    for user, name, level, cls, zone in snap.players[:12]:
        table.add_row(Text(user.title(), DIM), Text(name, "bold"), str(level), cls, zone)
    if len(snap.players) > 12:
        table.add_row("", Text(f"and {len(snap.players) - 12} more", DIM), "", "", "")
    return Panel(table, title="Players online", title_align="left", border_style=DIM, padding=(0, 1))


def events(snap, rows):
    text = Text(no_wrap=True, overflow="ellipsis")
    for line in snap.events[-rows:]:
        style = BAD if re.search(r"\berror\b|crash|assert|segmentation fault|exception", line, re.I) else \
            GOOD if "ready..." in line else ""
        text.append(line + "\n", style=style)
    if not snap.events:
        text.append("No server log for this session yet.", DIM)
    return Panel(text, title="Server log", title_align="left", border_style=DIM, padding=(0, 1))


def commands(play):
    table = Table.grid(padding=(0, 1))
    table.add_column(style=f"bold {GOLD}")
    table.add_column()
    table.add_column(style=DIM)
    for key, label, command in ACTIONS:
        table.add_row(key, label, f"living-azeroth {command}")
    if play:
        table.add_row("p", "Play", "")
    table.add_row("q", "Close", "the realm keeps running")
    return Panel(table, title="Commands", title_align="left", border_style=DIM, padding=(0, 1))


def screen(snap, play, height, message):
    top = Table.grid(expand=True, padding=(0, 1))
    top.add_column(ratio=3)
    top.add_column(ratio=2)
    top.add_row(Group(services(snap), world(snap)), commands(play))
    used = 4 + 10 + (min(len(snap.players), 13) + 5 if snap.players else 3) + 3
    parts = [header(snap), top, players(snap), events(snap, max(3, height - used))]
    if message:
        parts.append(Text(message, style=BUSY))
    return Group(*parts)


# ---------------------------------------------------------------- running

# Escape sequences: arrow keys, and the scroll wheel in terminals that turn it into arrow keys. None is a command.
ESCAPES = re.compile(r"\x1b(\[[0-9;?<>=]*[ -/]*[@-~]|O.|.)?", re.S)


class Keys:
    """Single key presses from the terminal, without echo. Escape sequences are dropped."""

    def __enter__(self):
        self.fd = sys.stdin.fileno()
        self.saved = termios.tcgetattr(self.fd)
        self.pending = []
        tty.setcbreak(self.fd)
        return self

    def __exit__(self, *_):
        termios.tcsetattr(self.fd, termios.TCSADRAIN, self.saved)

    def read(self, timeout):
        """The next key, or None when there was none within timeout (None waits)."""
        if not self.pending:
            ready, _, _ = select.select([self.fd], [], [], timeout)
            if ready:
                self.pending.extend(ESCAPES.sub("", os.read(self.fd, 1024).decode("utf-8", "ignore")))
        return self.pending.pop(0) if self.pending else None

    def forget(self):
        """Drop anything typed or scrolled while a command ran, so it is not taken as a choice."""
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.pending.clear()


def run_command(console, realm, command, title):
    """Run a living-azeroth command in the open, as if typed, and report how it ended."""
    shown = f"living-azeroth --realm {shlex.quote(str(realm))} {command}"
    console.clear()
    console.print(Panel(Text.assemble((title, f"bold {GOLD}"), "\n", ("$ " + shown, DIM)), box=box.HEAVY,
                        border_style=GOLD, padding=(0, 1)))
    if command == "logs":
        console.print(Text("Ctrl+C returns to the console; the realm keeps running.\n", DIM))
    previous = signal.signal(signal.SIGINT, signal.SIG_IGN)
    try:
        result = subprocess.run([sys.executable, str(CLI), "--realm", str(realm), command],
                                preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL))
    finally:
        signal.signal(signal.SIGINT, previous)
    ok = result.returncode == 0 or (command == "logs" and result.returncode in (-2, 130))
    console.print()
    console.print(Text("✓ Done" if ok else f"✗ Failed (exit {result.returncode}); the messages above say why.",
                       style=f"bold {GOOD if ok else BAD}"))
    return ok


def confirm(console, keys, question):
    keys.forget()
    console.print(Text.assemble(("\n" + question + " ", f"bold {BUSY}"), ("y/n", DIM)))
    while True:
        key = keys.read(None)
        if key is None:
            continue
        if key in ("y", "Y"):
            return True
        if key in ("n", "N", "q"):
            return False


def stopped(snap):
    return snap.services["world"][0] != "running"


def main(realm, start=False, stop=False, play=None):
    console = Console(highlight=False)
    names = zone_names(_client_data(realm))
    if stop:
        ok = run_command(console, realm, "stop", "Stopping Living Azeroth")
        if ok:
            console.print(Text("Characters, memories and the spending ledger are kept.", DIM))
        _pause(console, 4 if ok else None, verb="Closing")
        return 0 if ok else 1
    if not sys.stdin.isatty():
        raise SystemExit("The console needs a terminal. Use `living-azeroth status` in scripts.")
    snap = Snapshot(realm, names)
    if start and stopped(snap):
        run_command(console, realm, "start", "Starting Living Azeroth")
        time.sleep(1)

    latest = {"snap": Snapshot(realm, names)}
    wake = threading.Event()
    done = threading.Event()

    def refresh():
        while not done.is_set():
            try:
                latest["snap"] = Snapshot(realm, names)
            except Exception as error:  # a passing Docker hiccup must not end the console
                latest["error"] = str(error)
            wake.wait(REFRESH_SECONDS)
            wake.clear()

    reader = threading.Thread(target=refresh, daemon=True)
    reader.start()
    message = ""
    try:
        with Keys() as keys:
            keys.forget()  # scrolling while the realm started is not a command
            while True:
                with Live(console=console, screen=True, auto_refresh=False, transient=True) as live:
                    while True:
                        live.update(screen(latest["snap"], play, console.size.height, message), refresh=True)
                        key = keys.read(1.0)
                        if key is None:
                            continue
                        key = key.lower()
                        if key == "q":
                            return 0
                        action = next((a for a in ACTIONS if a[0] == key), None)
                        if action or (key == "p" and play):
                            break
                # Leave the live view to run a command where its output can be read.
                if key == "p":
                    subprocess.Popen(play, shell=True, start_new_session=True,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    message = "Starting the game in its own window."
                    continue
                _, label, command = action
                snap = latest["snap"]
                question = {"stop": "Stop the realm? Everyone online is logged out.",
                            "restart": "Restart the realm? Everyone online is logged out.",
                            "update": "Build the current source and restart on it? This can take a while.",
                            "backup": "Save all databases to the realm's backups folder?"}.get(command)
                if command == "start" and not stopped(snap):
                    message = "The realm is already running."
                    continue
                if command == "stop" and stopped(snap):
                    message = "The realm is already stopped."
                    continue
                if question:
                    console.clear()
                    console.print(header(snap))
                    if not confirm(console, keys, question):
                        message = ""
                        continue
                titles = {"start": "Starting Living Azeroth", "stop": "Stopping Living Azeroth",
                          "restart": "Restarting Living Azeroth", "update": "Updating Living Azeroth",
                          "logs": "Server log", "backup": "Backing up the databases"}
                ok = run_command(console, realm, command, titles[command])
                keys.forget()
                if command != "logs":
                    _pause(console, 2 if ok else None, keys)
                message = "" if ok else f"{label} failed; run it again to see why."
                wake.set()
    finally:
        done.set()
        wake.set()


def _client_data(realm):
    from cli import CACHE, CLIENT_DATA_VERSION  # the folder `living-azeroth setup` downloads into
    return read_state(realm).get("client_data", str(CACHE / "client-data" / CLIENT_DATA_VERSION))


def _pause(console, seconds, keys=None, verb="Returning"):
    if seconds is None:
        console.print(Text("Press any key to continue.", DIM))
        if keys:
            keys.read(None)
        elif sys.stdin.isatty():
            sys.stdin.readline()
        return
    console.print(Text(f"{verb} in {seconds} seconds…", DIM))
    if keys:
        keys.read(seconds)
    else:
        time.sleep(seconds)
