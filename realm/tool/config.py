"""Render a realm's server configuration from the source defaults and its realm.conf."""

import configparser
import json
import os
import re
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2]

# realm.conf section -> (config file it overrides, the .conf.dist it starts from)
SECTIONS = {
    "worldserver": ("worldserver.conf", "src/server/apps/worldserver/worldserver.conf.dist"),
    "authserver": ("authserver.conf", "src/server/apps/authserver/authserver.conf.dist"),
    "playerbots": ("modules/playerbots.conf", "modules/mod-playerbots/conf/playerbots.conf.dist"),
    "characters": ("modules/playerbots_characters.conf",
                   "modules/mod-living-characters/conf/playerbots_characters.conf.dist"),
    "auctionhouse": ("modules/mod_ahbot.conf", "modules/mod-ah-bot-plus/conf/mod_ahbot.conf.dist"),
    "progression": ("modules/progression_system.conf",
                    "modules/mod-progression-system/conf/progression_system.conf.dist"),
}
SCHEMAS = {"Login": "acore_auth", "World": "acore_world", "Character": "acore_characters",
           "Playerbots": "acore_playerbots"}
LEDGER_SCHEMA = "living_azeroth_ledger"
PROMPT_ADDITIONS = ("Character.system", "Foundation.system", "Memory.system", "Recall.system")


def read_settings(realm):
    parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None)
    parser.optionxform = str  # server config keys are case-sensitive
    parser.read(realm / "realm.conf", encoding="utf-8")
    return parser


def read_state(realm):
    path = realm / "realm.state"
    return json.loads(path.read_text()) if path.exists() else {}


def write_state(realm, state):
    path = realm / "realm.state"
    path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")


def database_info(password, schema):
    return f'"database;3306;root;{password};{schema}"'


def managed_keys(realm, state):
    """Keys owned by the tools: connections, paths and secrets. realm.conf cannot change them."""
    password = (realm / "secrets/database-password").read_text().strip()
    logins = {name + "DatabaseInfo": database_info(password, schema) for name, schema in SCHEMAS.items()
              if name != "Playerbots"}
    common = {"LogsDir": '"/realm/logs"', "SourceDirectory": '"/source"', "Updates.AutoSetup": "1"}
    key = realm / "secrets/model.key"
    characters = {
        "PBC.CharacterSystem.KeyFile": '"/realm/secrets/model.key"' if key.exists() else '""',
        "PBC.CharacterSystem.BudgetId": json.dumps(state.get("budget_id", "")),
        "PBC.CharacterSystem.BudgetDatabaseInfo": database_info(password, LEDGER_SCHEMA),
        "PBC.CharacterSystem.PromptsPath": '"/realm/prompts/enUS"',
        "PBC.CharacterSystem.RealmPhaseFile": '"/realm/prompts/realm-era.txt"',
        "PBC.CharacterSystem.NpcIdentities": '"/realm/config/npcs.json"',
        "PBC.CharacterSystem.RecordedFixture": '""',
    }
    return {
        "worldserver": {**logins, **common, "DataDir": '"/client-data"', "WorldServerPort": "8085",
                        "BindIP": '"0.0.0.0"', "RealmID": "1", "Console.Enable": "0",
                        "Updates.EnableDatabases": "7", "Updates.AllowedModules": '"all"'},
        "authserver": {"LoginDatabaseInfo": logins["LoginDatabaseInfo"], **common,
                       "RealmServerPort": "3724", "BindIP": '"0.0.0.0"'},
        "playerbots": {"PlayerbotsDatabaseInfo": database_info(password, SCHEMAS["Playerbots"]),
                       "Playerbots.Updates.EnableDatabases": "1"},
        "characters": characters,
        "dbimport": {**logins, **common, "Updates.EnableDatabases": "7", "Updates.AllowedModules": '"all"'},
    }


def render_file(source, destination, values):
    content = source.read_text(encoding="utf-8")
    for key, value in values.items():
        line = f"{key} = {value}"
        pattern = rf"(?m)^{re.escape(key)}\s*=.*$"
        if re.search(pattern, content):
            content = re.sub(pattern, lambda _: line, content, count=1)
        else:
            content += "\n" + line + "\n"
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".new")
    with open(temporary, "w", encoding="utf-8") as output:
        os.chmod(temporary, 0o600)
        output.write(content)
    temporary.replace(destination)


def stage_prompts(realm):
    """Module default prompts plus this repository's realm additions."""
    defaults = SOURCE / "modules/mod-living-characters/prompts/enUS"
    additions = (SOURCE / "realm/prompts/realm-additions.txt").read_text()
    target = realm / "prompts/enUS"
    target.mkdir(parents=True, exist_ok=True)
    for source in sorted(defaults.glob("*.default.txt")):
        text = source.read_text()
        (target / source.name).write_text(text)
        stem = source.name.removesuffix(".default.txt")
        if stem in PROMPT_ADDITIONS:
            (target / (stem + ".custom.txt")).write_text(text + "\n\n" + additions)
    (realm / "prompts/realm-era.txt").write_text((SOURCE / "realm/prompts/realm-era.txt").read_text())


def render(realm):
    """Write every server config file for this realm. Returns warnings for the user."""
    settings, state = read_settings(realm), read_state(realm)
    managed = managed_keys(realm, state)
    warnings = []
    config = realm / "config"
    for section, (name, dist) in SECTIONS.items():
        values = dict(settings[section]) if settings.has_section(section) else {}
        for key in list(values):
            if key in managed.get(section, {}):
                warnings.append(f"[{section}] {key} is set by the tools; ignored in realm.conf")
                del values[key]
        values.update(managed.get(section, {}))
        if section == "characters":
            has_key = (realm / "secrets/model.key").exists()
            if not has_key:
                values["PBC.CharacterSystem.Enable"] = "0"
                warnings.append("No model key (secrets/model.key): characters will not converse")
            else:
                values.setdefault("PBC.CharacterSystem.Enable", "1")
        render_file(SOURCE / dist, config / name, values)
    render_file(SOURCE / "src/tools/dbimport/dbimport.conf.dist", config / "dbimport.conf", managed["dbimport"])
    stage_prompts(realm)
    return warnings
