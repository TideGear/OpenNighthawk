# In-game settings

Open the menu with the tilde key. Use Up/Down to choose a row, Left/Right
to change a value, and Enter to open a page or choose an action. Escape goes
back one page. A description below the list explains the selected setting.
Blue values differ from their defaults; a star marks a pending restart change.
Valid launch values outside the presets remain visible and are preserved until
you deliberately choose a preset or reset defaults.

Audio Settings groups the DOS sound driver, music/effects sources, Roland
output, speaker speech and AdLib emulation. All seven controls remain available.
Advanced Settings contains machine timing, the instruction budget, simulation
steps per second and the CPU engine. These can affect game behavior.

Save writes option changes. Fullscreen applies immediately; restart-only
changes offer a restart. The restart keeps unrelated launch arguments and uses
the newly saved choices in place of conflicting arguments. An active explicit
sound-driver selection is preserved so SETUP receives its normal joystick and
sound answers. Selecting Sound Driver **Ask** deliberately shows those questions.

Reset All Settings to Default selects the menu defaults and clears custom
keyboard/controller mappings and custom fix choices when you Save. Close
without saving cancels that pending reset. Game, MT-32 ROM and Mac recording
folders are kept. The default sound driver is **Ask**, so a factory reset can
bring SETUP's questions back. Original pilot/mission options remain the game's
own settings.

The Keys tab (Tab switches to it from Settings, or Ctrl+Alt+K) is also the key
remapper, so you can see what a key does while you change it. Up/Down moves
through the rows that can be remapped (every single-key control of the first
four pages); Enter then waits for the new key, and Escape cancels. The
assignment is a swap: the key you press sends the row's game key and the old
game key sends what the pressed key sent, so no two keys do the same thing.
Backspace restores the row's default. A row whose binding is not the default
shows the new key in blue. Host shortcuts continue using physical keys. Changes
take effect and save immediately. Settings no longer has Remap a Key or Clear Key
Remaps; Reset All Settings to Default still clears the remaps. Controller buttons are assigned there too: press a button instead of a key while a
row waits, and Backspace on a row also clears its buttons (shown as "+ pad N").
Settings no longer lists the controller-button rows. Fix-picker selections are saved as a custom
set and take effect after restart.

Folders accepts typed or pasted paths. Enter keeps an edit, Escape undoes it,
and Backspace clears a folder choice. Save writes folder changes. Choose Fixes
wraps complete descriptions and scrolls by their actual rendered height.

Developer verification is documented in [host-acceptance.md](host-acceptance.md).

Terrain dots (Remastered only, applied at once) sets how far the small dark dots that add detail to plain ground are drawn: Original keeps the game's own reach
(default), Near, Far and Maximum draw them as far as the Draw distance setting reaches. Lit points (lights) always follow Draw distance.

Far object order (Remastered only, applied at once): Exact (default) sorts far scenery with the order every picture comparison was made against; Fast uses a cheaper sort
(about 0.8 ms less work per frame at Maximum) that lets objects tied on distance swap places, which changes a few edge pixels in some frames.

Draw distance (Remastered only, applied at once) sets how far scenery is drawn: Maximum draws every ring out to the limit of the game's own arithmetic, Far
and Near shorten the reach of the smallest objects, and Original keeps the game's short reach. Farther costs picture rate; see
[scenery-distance.md](scenery-distance.md).
