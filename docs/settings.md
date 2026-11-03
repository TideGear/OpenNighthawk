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

Remap a Key opens a list of all 101 supported game keys. The left column is the
physical key; the right column is what the game receives. Blue mappings differ
from the original binding. Enter opens an output-key list, including Escape,
Enter and punctuation keys. Enter assigns; Escape cancels the selection.
Backspace restores the selected physical key's default. Page Up/Down and
Home/End move through either list. Host shortcuts continue using physical keys.
Assignments and Clear Key Remaps take effect and save immediately, as do the
existing controller-key assignment actions. Fix-picker selections are saved
as a custom set and take effect after restart.

Folders accepts typed or pasted paths. Enter keeps an edit, Escape undoes it,
and Backspace clears a folder choice. Save writes folder changes. Choose Fixes
wraps complete descriptions and scrolls by their actual rendered height.

Developer verification is documented in [host-acceptance.md](host-acceptance.md).
