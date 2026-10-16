# Volume controls

Three volume keys on one tile: quieter, mute and louder. Each key performs the Home Assistant action you give it, so the
same tile drives a media player, an amplifier behind an IR remote, or anything a script can reach.

Give a key an action and it appears; leave it empty and it is left out, and the keys that are there share the room. They
sit together in the middle of the tile:

- On a tile as wide as it is high or wider (1x1, 2x1, 2x2 and so on) they stand **across**, as minus, mute, plus.
- On a tile taller than it is wide (1x2, 1x3) they stand **down**, louder at the top, then mute, then quieter.

A key is never higher than 14 mm nor wider than half again its height, so a bigger tile only gives the keys room. The
tile is one cell at least and up to 3x3.

## Set up

1. In Tessera, open **Plugins**, choose **Volume controls** and tick your screens. Each screen builds once with the
   plugin.
2. In Layout, place the **Volume controls** tile from the library's Plugins group.
3. In the inspector, choose the entity the keys act on: a media player, a remote, a script and so on.
4. Type an action for each key you want, for example:

   | Key | Media player | IR remote or amplifier |
   |---|---|---|
   | Quieter | `media_player.volume_down` | `remote.send_command command=VOLUME_DOWN` |
   | Mute | `media_player.volume_mute is_volume_muted=true` | `remote.send_command command=MUTE` |
   | Louder | `media_player.volume_up` | `remote.send_command command=VOLUME_UP` |

   The command names are whatever your integration or learned IR codes call them. Note that
   `media_player.volume_mute is_volume_muted=true` only ever mutes. To toggle, point the key at a script (below) or
   use your remote's own mute command.

5. Make sure the screen may perform Home Assistant actions. Tessera turns this on for a screen it added to Home
   Assistant; for a screen added by hand, tick **Allow the device to perform Home Assistant actions** in the ESPHome
   integration's options for that device.

## What a key's action can say

A key's text is the action, then optionally an entity of its own, then optionally one field, separated by spaces:

- `media_player.volume_up`: the action, on the tile's entity.
- `media_player.volume_mute is_volume_muted=true`: the action with one field, on the tile's entity. The value runs to
  the end of the text, so it may contain spaces.
- `script.turn_on script.toggle_mute`: the action on another entity than the tile's. A script can do whatever one
  action cannot, such as toggle the mute or step the volume several times.
- `media_player.volume_up media_player.amplifier`: the action on another media player.

A key whose text is not an action shows its icon in red, and a tap sends nothing. At most 64 characters per key, and
one field.

## On the screen

- A tap on a key lights it blue while the action goes out, or red when it could not be sent (no entity, no action, or
  the screen may not perform actions).
- With a media player as the tile's entity, the mute key is lit while the player is muted.
- The icons go grey while the entity is unavailable.
- One tap sends one step: the tile does not repeat while a finger is held, because a plugin's tile only gets taps.
  How big a step is depends on the integration.
- Works on every board, the CYD included. On a single cell of a small screen the keys get small; make the tile bigger
  to give them more room.

## Limiting the volume

The tile sends the actions you give it and has no volume limit of its own. Home Assistant has no built-in per-player
limit either, so where a top or bottom level matters (a shop, a bar, a conference room, a child's room), put the limit
where it cannot be skipped. From the strongest:

1. **On the device.** Many amplifiers, receivers and speakers have a "maximum volume" setting in their own app. The
   hardware enforces it whatever sends the command, whether this tile, Home Assistant or the device's own remote.
2. **A wrapper player in Home Assistant.** The [Universal media
   player](https://www.home-assistant.io/integrations/universal/) can replace a player's `volume_set`, `volume_up`,
   `volume_down` and `volume_mute` commands. Point the tile's entity (or each key) at the wrapper instead of the real
   player. An untested sketch that keeps `volume_set` between 20 % and 60 %:

   ```yaml
   media_player:
     - platform: universal
       name: Clamped receiver
       children:
         - media_player.receiver
       commands:
         volume_set:
           action: media_player.volume_set
           target:
             entity_id: media_player.receiver
           data:
             volume_level: "{{ [[volume_level, 0.2] | max, 0.6] | min }}"
       attributes:
         volume_level: media_player.receiver|volume_level
   ```

   The docs do not describe clamping, so check it on your own system. A plain `volume_up` or `volume_down` steps by
   whatever the integration decides and can pass the limit, so override those too, or give the keys a script that
   checks the level first (`script.turn_on script.volume_up_limited`).
3. **Put it back afterwards.** A blueprint such as [Media Player - Volume
   limit](https://community.home-assistant.io/t/media-player-volume-limit) watches `volume_level` and resets the player
   when it goes above a limit. It reacts after the fact, so there is a short spike, and it covers the upper limit only.

A slider on Tessera's own volume tile can be limited with an `input_number` helper: Home Assistant enforces the
helper's minimum and maximum, and an automation copies its value to the player. That limits the slider only, so use it
together with one of the above.

## Good to know

- A key runs whatever action you type for it, on whatever entity you name. The manifest lists the actions the tile is
  made for (media player, remote, script, button, scene and switch actions), but the screen does not hold a key to that
  list: anyone who can edit the layout can make a key perform any action the screen is allowed to.
- Nothing leaves your home: the tile uses no internet service and no Home Assistant entity but the ones you choose.

## How it works

- `tessera-plugin.yaml`: one tile, `volume`, 1x1 to 3x3, that belongs to an entity of the domains it acts on and reads
  its `is_volume_muted` attribute, with one text option per key.
- `components/volume_controls/volume_command.h`: reads a key's text into an action, a target and a field (the same code
  as the Transport controls plugin).
- `components/volume_controls/volume_controls.cpp`: lays the keys out across or down the middle of the tile, finds the
  key under the finger (`tessera::tap_point`) and sends its action (`tessera::action`).
