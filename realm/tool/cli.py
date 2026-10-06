"""living-azeroth: set up, build and run a Living Azeroth realm."""

import argparse
import getpass
import hashlib
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import urllib.request
import zipfile

from config import SOURCE, ledger_user, read_settings, read_state, render, shared_budget, write_state

COMPOSE_FILE = SOURCE / "realm/compose.yaml"
SHARED_BUDGET_FILE = SOURCE / "realm/compose.shared-budget.yaml"
USER_CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "living-azeroth"
CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "living-azeroth"
DEFAULT_POINTER = USER_CONFIG / "default-realm"
CLIENT_DATA_VERSION = "v20.0"
CLIENT_DATA_URL = f"https://github.com/wowgaming/client-data/releases/download/{CLIENT_DATA_VERSION}/data.zip"


# ---------------------------------------------------------------- realm folder

def find_realm(path):
    if path:
        return Path(path).expanduser().resolve()
    if os.environ.get("LIVING_AZEROTH_REALM"):
        return Path(os.environ["LIVING_AZEROTH_REALM"]).expanduser().resolve()
    if DEFAULT_POINTER.exists():
        return Path(DEFAULT_POINTER.read_text().strip())
    raise SystemExit("No realm chosen. Run `living-azeroth setup`, or pass --realm PATH.")


def require_realm(realm):
    if not read_state(realm).get("ready"):
        raise SystemExit(f"{realm} is not a finished realm. Run `living-azeroth setup --realm {realm}`.")
    return realm


def image_tag():
    digest = hashlib.sha256((SOURCE / "realm/Dockerfile").read_bytes()).hexdigest()[:12]
    return f"living-azeroth/toolchain:{digest}"


def environment(realm):
    settings, state = read_settings(realm), read_state(realm)
    section = settings["realm"]
    return {
        **os.environ,
        "LA_PROJECT": state["project"],
        "LA_DATABASE_VOLUME": state["database_volume"],
        "LA_IMAGE": image_tag(),
        "LA_UID": str(os.getuid()), "LA_GID": str(os.getgid()),
        "LA_SOURCE": str(SOURCE), "LA_REALM": str(realm), "LA_CACHE": str(CACHE),
        "LA_CLIENT_DATA": state.get("client_data", str(CACHE / "client-data" / CLIENT_DATA_VERSION)),
        "LA_BIND": section.get("bind", "127.0.0.1"),
        "LA_AUTH_PORT": section.get("auth_port", "3724"),
        "LA_WORLD_PORT": section.get("world_port", "8085"),
        "LA_BUILD_JOBS": section.get("build_jobs", "4"),
        **({"LA_SHARED_BUDGET_NETWORK": read_state(owner)["project"] + "_realm"}
           if (owner := budget_owner(realm)) else {}),
    }


def compose(realm, *args, check=True, extra_env=None):
    env = environment(realm)
    env.update(extra_env or {})
    files = [COMPOSE_FILE] + ([SHARED_BUDGET_FILE] if budget_owner(realm) else [])
    command = ["docker", "compose", *(item for path in files for item in ("-f", str(path))), *args]
    return subprocess.run(command, env=env, check=check)


def tool(realm, *args, extra_env=None, env_names=()):
    ensure_image()
    passthrough = [item for name in env_names for item in ("-e", name)]
    return compose(realm, "--profile", "tool", "run", "--rm", "--no-deps", *passthrough, "tool", *args,
                   extra_env=extra_env)


def database_tool(realm, *args, extra_env=None, env_names=()):
    return tool(realm, "python3", "/source/realm/tool/database.py", *args, extra_env=extra_env,
                env_names=env_names)


# ---------------------------------------------------------------- steps

def ensure_image():
    tag = image_tag()
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        print(f"Building the toolchain image {tag} (once)...")
        subprocess.run(["docker", "build", "-t", tag, "-f", str(SOURCE / "realm/Dockerfile"),
                        str(SOURCE / "realm")], check=True)


def ensure_client_data(realm):
    target = Path(environment(realm)["LA_CLIENT_DATA"])
    if (target / "maps").is_dir() and (target / "dbc").is_dir():
        return
    target.mkdir(parents=True, exist_ok=True)
    archive = target.parent / f"{CLIENT_DATA_VERSION}.zip"
    print(f"Downloading server map data ({CLIENT_DATA_VERSION}, about 1.5 GB)...")
    urllib.request.urlretrieve(CLIENT_DATA_URL, archive)
    with zipfile.ZipFile(archive) as data:
        data.extractall(target)
    archive.unlink()


def prepare_folders(realm):
    for name in ("config", "logs", "backups", ".home", "prompts"):
        (realm / name).mkdir(parents=True, exist_ok=True)
    (CACHE / "ccache").mkdir(parents=True, exist_ok=True)


def budget_owner(realm):
    """The finished realm whose budget this realm spends from, or None for its own budget."""
    owner = shared_budget(realm)
    if owner is None:
        return None
    if owner == realm:
        raise SystemExit("shared_budget in realm.conf names this realm itself.")
    if not read_state(owner).get("ready") or not read_state(owner).get("budget_id"):
        raise SystemExit(f"shared_budget: {owner} is not a finished realm with a budget.")
    return owner


def prepare_shared_budget(realm):
    """Start the owner's database and give this realm its limited ledger login there."""
    owner = budget_owner(realm)
    if owner is None:
        return
    password = realm / "secrets/ledger-password"
    if not password.exists():
        write_secret(password, secrets.token_hex(24))
    compose(owner, "up", "-d", "--wait", "database")
    database_tool(owner, "grant-ledger", ledger_user(read_state(realm)),
                  extra_env={"LA_LEDGER_PASSWORD": password.read_text().strip()}, env_names=("LA_LEDGER_PASSWORD",))
    print(f"Spending from the budget of {owner.name}.")


def render_config(realm):
    owner = budget_owner(realm)
    if owner and not (realm / "secrets/ledger-password").exists():
        write_secret(realm / "secrets/ledger-password", secrets.token_hex(24))
    for warning in render(realm):
        print("Note:", warning)


def build(realm, install=False, targets=()):
    prepare_folders(realm)
    args = ["bash", "/source/realm/build.sh"] + (["--install"] if install else []) + list(targets)
    tool(realm, *args, extra_env={}, env_names=())


def sync_realm(realm):
    section = read_settings(realm)["realm"]
    database_tool(realm, "sync-realm", section.get("name", "Living Azeroth"),
                  section.get("address", "127.0.0.1"), section.get("world_port", "8085"))


def start(realm):
    ensure_image()
    prepare_shared_budget(realm)
    render_config(realm)
    compose(realm, "up", "-d", "--wait", "database")
    sync_realm(realm)
    compose(realm, "up", "-d", "auth", "world")
    print(f"Started. The server is ready for logins when `ready...` appears in {realm / 'logs/Server.log'}"
          " (`living-azeroth logs`).")


def stop(realm):
    compose(realm, "stop", "--timeout", "60")


def account(realm, name, gm_level, secret=None):
    if secret is None:
        secret = getpass.getpass(f"Password for {name}: ")
        if secret != getpass.getpass("Again: "):
            raise SystemExit("The passwords differ.")
    if not secret:
        raise SystemExit("A password is required.")
    args = ["account", name] + (["--gm", str(gm_level)] if gm_level is not None else [])
    database_tool(realm, *args, extra_env={"LA_ACCOUNT_PASSWORD": secret}, env_names=("LA_ACCOUNT_PASSWORD",))


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "realm"


# ---------------------------------------------------------------- commands

def write_secret(path, value):
    with open(path, "w") as output:
        os.chmod(path, 0o600)
        output.write(value + "\n")


def cmd_setup(args):
    """Create a realm, or finish one whose setup stopped part-way. Each step runs once."""
    realm = Path(args.realm or Path.home() / "living-azeroth-realm").expanduser().resolve()
    if read_state(realm).get("ready"):
        raise SystemExit(f"{realm} is already set up. Use `living-azeroth --realm {realm} start`.")
    shutil.which("docker") or sys.exit("Docker is required: https://docs.docker.com/engine/install/")
    realm.mkdir(parents=True, exist_ok=True)

    if not (realm / "realm.conf").exists():
        example = (SOURCE / "realm/realm.example.conf").read_text()
        name = args.name or input("Realm name [Living Azeroth]: ").strip() or "Living Azeroth"
        example = re.sub(r"(?m)^name = .*$", f"name = {name}", example, count=1)
        for key in ("auth_port", "world_port"):
            value = getattr(args, key)
            if value:
                example = re.sub(rf"(?m)^{key} = .*$", f"{key} = {value}", example, count=1)
        (realm / "realm.conf").write_text(example)
    state = read_state(realm)
    if "project" not in state:
        state["project"] = "living-azeroth-" + slug(realm.name)
        state["database_volume"] = state["project"] + "-database"
        if args.client_data:
            state["client_data"] = str(Path(args.client_data).expanduser().resolve())
        write_state(realm, state)

    secrets_dir = realm / "secrets"
    secrets_dir.mkdir(mode=0o700, exist_ok=True)
    if not (secrets_dir / "database-password").exists():
        write_secret(secrets_dir / "database-password", secrets.token_hex(24))
        key = Path(args.model_key_file).read_text().strip() if args.model_key_file else \
            getpass.getpass("OpenRouter API key for character conversations (Enter to skip): ").strip()
        if key:
            write_secret(secrets_dir / "model.key", key)

    ensure_client_data(realm)
    print("Building the server (the first build takes a while)...")
    build(realm, install=True)
    render_config(realm)
    compose(realm, "up", "-d", "--wait", "database")
    dollars = read_settings(realm)["realm"].get("budget_dollars", "5")
    database_tool(realm, "init", "--budget-dollars", dollars)
    if not read_state(realm).get("admin"):
        admin = args.admin or input("Administrator account name: ").strip()
        secret = Path(args.admin_password_file).read_text().strip() if args.admin_password_file else None
        account(realm, admin, 3, secret)
        state = read_state(realm)
        state["admin"] = admin.upper()
        write_state(realm, state)
    compose(realm, "stop")
    if args.make_default or not DEFAULT_POINTER.exists():
        USER_CONFIG.mkdir(parents=True, exist_ok=True)
        DEFAULT_POINTER.write_text(str(realm) + "\n")
    state = read_state(realm)
    state["ready"] = True
    write_state(realm, state)
    print(f"\nRealm ready in {realm}. Start it with `living-azeroth start`.")


def main():
    # --realm may come before or after the command.
    realm_option = argparse.ArgumentParser(add_help=False)
    realm_option.add_argument("--realm", default=argparse.SUPPRESS,
                              help="realm folder (default: the last one set up)")
    parser = argparse.ArgumentParser(prog="living-azeroth", description=__doc__, parents=[realm_option])
    commands = parser.add_subparsers(dest="command", required=True, metavar="command")

    def command(name, help):
        return commands.add_parser(name, help=help, parents=[realm_option])

    setup = command("setup", "create a new realm folder and set it up")
    setup.add_argument("--name")
    setup.add_argument("--admin", help="administrator account name")
    setup.add_argument("--admin-password-file")
    setup.add_argument("--model-key-file", help="file containing the OpenRouter API key")
    setup.add_argument("--client-data", help="use already-extracted server map data from this folder")
    setup.add_argument("--auth-port", dest="auth_port")
    setup.add_argument("--world-port", dest="world_port")
    setup.add_argument("--make-default", action="store_true", help="make this the default realm")

    command("start", "start the realm")
    command("stop", "stop the realm (characters and data are kept)")
    command("restart", "stop, then start with current settings")
    command("status", "show whether the realm is running")
    logs = command("logs", "follow the world server log")
    logs.add_argument("--auth", action="store_true", help="the login server log instead")
    build_parser = command("build", "compile changed source (the realm keeps running)")
    build_parser.add_argument("targets", nargs="*")
    command("update", "build, then restart the realm on the new build")
    command("config", "regenerate server configuration from realm.conf")
    command("test", "build and run the unit tests")
    account_parser = command("account", "create an account or change its password")
    account_parser.add_argument("name")
    account_parser.add_argument("--gm", type=int, choices=range(0, 4), help="GM level (3 = administrator)")
    command("budget", "show the character system's spending")
    command("backup", "save all realm databases to the realm's backups folder")

    args = parser.parse_args()
    args.realm = getattr(args, "realm", None)
    if args.command == "setup":
        return cmd_setup(args)
    realm = require_realm(find_realm(args.realm))
    if args.command == "start":
        start(realm)
    elif args.command == "stop":
        stop(realm)
    elif args.command == "restart":
        stop(realm)
        start(realm)
    elif args.command == "status":
        compose(realm, "ps")
    elif args.command == "logs":
        name = "Auth.log" if args.auth else "Server.log"
        subprocess.run(["tail", "-n", "40", "-F", str(realm / "logs" / name)])
    elif args.command == "build":
        build(realm, targets=args.targets)
    elif args.command == "update":
        build(realm)
        stop(realm)
        build(realm, install=True)
        render_config(realm)
        compose(realm, "up", "-d", "--wait", "database")
        database_tool(realm, "init")
        start(realm)
    elif args.command == "config":
        render_config(realm)
        print(f"Wrote {realm / 'config'}.")
    elif args.command == "test":
        build(realm, targets=["unit_tests"])
        tool(realm, "/realm/build/src/test/unit_tests", "--gtest_brief=1")
        tool(realm, "env", "CXX=clang++-18", "python3", "/source/modules/mod-playerbots/tests/population/run.py")
    elif args.command == "account":
        compose(realm, "up", "-d", "--wait", "database")
        account(realm, args.name, args.gm)
    elif args.command == "budget":
        owner = budget_owner(realm) or realm
        if owner != realm:
            print(f"Shared with {owner}:")
        compose(owner, "up", "-d", "--wait", "database")
        database_tool(owner, "budget")
    elif args.command == "backup":
        compose(realm, "up", "-d", "--wait", "database")
        tool(realm, "bash", "-c", 'stamp=$(date -u +%Y%m%dT%H%M%SZ); MYSQL_PWD="$(cat /realm/secrets/database-password)" '
             "mysqldump --host=database --user=root --single-transaction --no-tablespaces --set-gtid-purged=OFF "
             "--databases acore_auth acore_characters acore_world acore_playerbots living_azeroth_ledger "
             '| gzip > "/realm/backups/$stamp.sql.gz" && echo "Saved backups/$stamp.sql.gz"')
