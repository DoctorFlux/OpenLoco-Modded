# Hello World

This mod logs one line when it is enabled. It does not change the simulation and it contains no Locomotion assets.

Install it by copying this folder into a mods directory, then list its id in that directory's `enabled.json`:

```json
{
    "enabled": ["openloco.sample.hello"]
}
```

Check it without starting a game:

```
OpenLoco.exe mods --mods_path <mods-directory>
```

On a normal launch, OpenLoco also reads `%APPDATA%\OpenLoco\mods` when that folder already exists. Mods that are not listed in `enabled.json` are reported and not run.
