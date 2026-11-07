import sys
sys.path.pop(0)
from pathlib import Path
p=Path('outputs/arceus-integer-timer-triage.txt');s=p.read_text()
s=s.replace('second pass inventory updated by third-pass follow-up','second pass inventory updated through fourth-pass follow-up')
a=s.index('Respawn fade hold, main+24EE204');z=s.index('\nLOWER-CONFIDENCE / INCOMPLETE CANDIDATES',a)
s=s[:a]+'''Respawn fade hold, main+24EE204
  Fourth-pass mapping: state +240 == 4 owns counter +2A0. It increments the
  field but compares its OLD value with respawn_fade_keep_frame_count, waiting
  while old <= threshold. From zero, threshold N takes N+2 callbacks to the
  expiry decision. Tests preserve this original behavior; it is not classified
  as an off-by-one bug. The hold and common fade animation are separate clocks.

Climbing movement stopping blend, main+2549FA0
  State +D00 == 2 uses +D04 for elapsed progression and eases saved vectors
  toward zero. Initializer254B254 stores 64-bit value2 at +D00, simultaneously
  setting state2 and resetting +D04. Expiration compares the NEW count against
  climbing_move_stop_frame. This function also invokes the ride blink helper;
  it cannot safely be throttled wholesale to fix the stopping blend.

Ride search-voice interval and dash-effect ramp, main+2587684
  Fourth-pass correction: +D08 controls a search-voice interval, potentially
  issuing PLAY_PV_RD_217_00_SEARCH, then resetting. It is not the dash ramp.
  The real dash ramp is +D20, aliased through X8 at2588038. It increments and
  clamps, then divides by the configured duration into float+D00. Both named
  dash-effect easing paths consume that normalized value. Live rates pending.

Wormhole delayed effect cleanup, main+1B5BBB8
  Entries in the queue at owner+440/+448 have 0x18 stride and begin with a
  signed countdown. Initializer1B5E5A0 sets it to30. Expiration processes
  retained effects through2B90D40 and erases the entry. This does not identify
  the overall space-time-distortion duration. The nearby wormhole_out_keep_frame
  parameter instead feeds the player facial-expression helper24D35B8.
''' +s[z:]
a=s.index('LOWER-CONFIDENCE / INCOMPLETE CANDIDATES');z=s.index('REJECTED OR DEPRIORITIZED MATCHES',a)
s=s[:a]+s[z:]
s+='\nFourth-pass execution checks and corrected field mappings:\narceus-engine-research-pass4.txt and its physics/AI/field/attention companion reports.\n'
p.write_text(s)
p=Path('outputs/arceus-engine-research-pass3.txt');s=p.read_text()
s=s.replace('2026-11-06\n','2026-11-06\n\nFollow-up: arceus-engine-research-pass4.txt traces actual physics translation,\nAI mixed clocks, and timer expiration rules. The package hash in this report\nidentifies the historical third-pass candidate.\n',1)
p.write_text(s)
print('Earlier timer inventory updated with corrected fourth-pass classifications.')
