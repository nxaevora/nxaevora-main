import sys
sys.path.pop(0)
from pathlib import Path
p=Path('outputs/arceus-integer-timer-triage.txt')
s=p.read_text(encoding='utf-8-sig')
s=s.replace('2026-11-06, second offline research pass','2026-11-06, second pass inventory updated by third-pass follow-up')
s=s.replace('  No capture-director correction or new live probe installed in this pass.','  Third-pass update: a guarded observation probe is now in the research\n  candidate. No timing correction is enabled. The exact stage/parameter map\n  is in arceus-engine-research-pass3.txt.')
s=s.replace('  Next: trace the corresponding initialization fields and live cadence.','  Third-pass update: 19C6760 takes a slot index in W1 and duration in W2;\n  each slot has a 0x80 stride. A separate +670 elapsed counter is compared\n  against look_start_keep_frame. It is not the initializer for these slots.\n  Live cadence still needs measurement.')
a=s.index('Buddy reaction / attention holds, main+24D17DC')
z=s.index('\nRespawn fade hold,',a)
s=s[:a]+'''Player facial-expression holds, main+24D17DC
  Third-pass parameter tracing identifies the player_face group. +280 is an
  expression-override countdown initialized through helper 24D35B8, either
  from an explicit duration or face_change_event_default_frame. The vigilance
  ending and two buddy-interaction keep-frame settings feed this helper.
  +2A4 is vigilance_end_delay_frame. +298 counts surprise elapsed updates;
  +29C counts rejoice elapsed updates. The latter two compare against their
  named keep-frame parameters. These are separate from eyelid blink timing.
  Exact addresses and initializer paths are in arceus-engine-research-pass3.txt.
  No facial timer correction or new face probe has been enabled.
''' +s[z:]
s=s.replace('''Skip/fade state function main+1458300 and helper main+145E1C0:
  Both increment +3BC in selected paths; skip_fade_frame occurs in the owner.
  Need state/parameter mapping before deciding whether this counter measures
  a skip hold, fade duration, or some other state lifetime.

''','')
s=s.replace('REJECTED OR DEPRIORITIZED MATCHES\n','''REJECTED OR DEPRIORITIZED MATCHES

main+1458300 / 145E1C0, field +3BC:
  Third-pass follow-up confirms this is a substate selector, not elapsed time.
  The actual skip fade is passed to common fade wrapper C264CC; update C21490
  uses +538/+53C for elapsed/duration. See the third-pass fade findings.
''')
s+='\nThird-pass follow-up: arceus-engine-research-pass3.txt adds common fades,\nUI elapsed-clock analysis, capture-stage mapping, and face/look initializer tracing.\n'
p.write_text(s,encoding='utf-8')
p=Path('outputs/arceus-engine-research-pass2.txt');s=p.read_text(encoding='utf-8-sig')
s=s.replace('2026-11-06\n','2026-11-06\n\nFollow-up: arceus-engine-research-pass3.txt adds fade/capture diagnostics,\nUI elapsed-clock evidence and more precise facial-expression timer mappings.\nThe candidate hash below is the historical second-pass package.\n',1)
p.write_text(s,encoding='utf-8')
p=Path('work/pass3/ui-capture-checks.txt');b=p.read_bytes()
s=b.decode('utf-16' if b.startswith(b'\xff\xfe') else 'utf-8-sig')
Path('outputs/arceus-ui-capture-instruction-checks.txt').write_text(s,encoding='utf-8')
print('Research reports and instruction results updated.')
