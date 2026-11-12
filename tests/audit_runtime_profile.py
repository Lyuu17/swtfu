"""Source-bound inventory of the current native60 profile for integration review."""
import hashlib
import json
import struct
from pathlib import Path
import sys

import capstone
import pefile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from swtfu.tfu1.fps import profile as FrameRateFix


def wrapper_references(pe):
    """Overinclusive raw encoding scan; zero hits also excludes decoded hits."""
    result = []
    for target, size in ((0x408B20, 7), (0x410770, 0x1D)):
        transfers, pointers = [], []
        needle = struct.pack('<I', target)
        for section in pe.sections:
            data = section.get_data()
            base = pe.OPTIONAL_HEADER.ImageBase + section.VirtualAddress
            position = data.find(needle)
            while position >= 0:
                pointers.append(base + position)
                position = data.find(needle, position + 1)
            if not section.Characteristics & 0x20000000:
                continue
            for opcode in (b'\xe8', b'\xe9'):
                position = data.find(opcode)
                while position >= 0:
                    if position + 5 <= len(data):
                        destination = (base + position + 5 + struct.unpack_from(
                            '<i', data, position + 1)[0]) & 0xFFFFFFFF
                        if destination == target:
                            transfers.append(base + position)
                    position = data.find(opcode, position + 1)
        assert not transfers and not pointers, hex(target)
        result.append({'address': hex(target), 'size': size,
                       'bytes': pe.get_data(target-pe.OPTIONAL_HEADER.ImageBase, size).hex(),
                       'direct_rel32_call_or_jump_encodings': transfers,
                       'absolute_pointer_literals': pointers})
    return {'wrappers': result,
            'scope': 'Raw section bytes, including unaligned candidates; excludes short jumps, computed/encoded pointers, runtime-generated code and external modules.',
            'unreachable_proven': False}


def main():
    source = (ROOT.parent / 'analysis' / 'SWTFU.decoded.exe').read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    assert digest == '7fafe219fd9a3ce0c146a4322a0da45750b056dc42d743cd7c28827ea237479c'
    pe = pefile.PE(data=source)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    # Verify native instructions, not cached decompiler comment addresses.
    # Virtual dispatch identities and all alternative callers remain open.
    ownership_anchors = {
        0x41BF0D: 'call 0x458ee0',
        0x459049: 'mov eax, dword ptr [edx + 0x3c]',
        0x459052: 'call eax',
        0x759501: 'call 0x457a30',
        0x759518: 'call 0x4580b0',
        0x457A86: 'call 0x455fd0',
        0x457A98: 'call 0x4557a0',
        0xC362B7: 'call 0xc34ce0',
        0xC362C2: 'call 0xc34ea0',
        0x7C2300: 'call 0xc36290',
        0x7C2307: 'call 0x7ae6d0',
        0x7C24C5: 'call 0x7ae6d0',
        0x74DC2D: 'mov dword ptr [esi], 0xfb8b4c',
        0x755990: 'mov dword ptr [esi], 0xfb8b4c',
        0x41BF07: 'mov ecx, dword ptr [esi + 0x90]',
        0x7C07DE: 'mov dword ptr [esi], 0xfba660',
        0x7C13FB: 'mov dword ptr [esi], 0xfba660',
        0x7C22E9: 'movss xmm0, dword ptr [0x10d4008]',
        0x7C22FC: 'jp 0x7c2321',
        0x4189BA: 'call 0xc32e70',
        0x4189B4: 'mov ecx, dword ptr [0x3a1be80]',
        0xC57908: 'mov ecx, dword ptr [0x3a1be80]',
        0xC5790F: 'call 0xc343a0',
        0xC34436: 'call 0x7c13c0',
        0xC3444D: 'call 0xc340a0',
        0xC34452: 'mov dword ptr [eax], esi',
        0xC32E80: 'mov ecx, dword ptr [esi + 0x10]',
        0xC32E85: 'mov edx, dword ptr [eax + 0x14]',
        0xC32E88: 'call edx',
        0xC340F8: 'add eax, 0x10',
        0xC34102: 'lea eax, [edx + 0x10]',
        0x41E2D5: 'mov edi, ecx',
        0x41E891: 'lea ecx, [edi + 0x90]',
        0x41E897: 'call 0x4088d0',
        0x408925: 'call 0x755960',
        0x4088FA: 'mov esi, ecx',
        0x40892E: 'mov dword ptr [esi], eax',
        0x41077E: 'mov ecx, dword ptr [esi + 0x90]',
        0x410784: 'call 0x458ee0',
        0x69DF60: 'mov eax, dword ptr [ecx + 0xce0]',
        0x69DF66: 'fld dword ptr [eax + 8]',
        0x69DF76: 'fld dword ptr [eax + 0xc]',
        0x6B8E09: 'call 0x6a8cd0',
        0x6B8E0E: 'fstp dword ptr [esi + 8]',
        0x6B8E24: 'fstp dword ptr [esi + 0xc]',
    }
    ownership_evidence = []
    for address, expected in ownership_anchors.items():
        instruction = next(decoder.disasm(pe.get_data(
            address-pe.OPTIONAL_HEADER.ImageBase, 15), address))
        assert f'{instruction.mnemonic} {instruction.op_str}'.strip() == expected, hex(address)
        ownership_evidence.append({'address': hex(address), 'instruction': expected,
                                   'bytes': instruction.bytes.hex()})
    assert struct.unpack('<I', pe.get_data(
        0xFB8B4C + 0x3C - pe.OPTIONAL_HEADER.ImageBase, 4))[0] == 0x759080
    assert struct.unpack('<I', pe.get_data(
        0xFBA660 + 0x14 - pe.OPTIONAL_HEADER.ImageBase, 4))[0] == 0x7C2200
    def rtti(table, expected):
        def word(address):
            return struct.unpack('<I', pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase, 4))[0]
        locator = word(table-4)
        assert word(locator) == 0  # x86 absolute-pointer RTTI locator
        descriptor = word(locator+12)
        name = pe.get_data(descriptor-pe.OPTIONAL_HEADER.ImageBase+8, 160).split(b'\0', 1)[0]
        assert name == expected
        return {'locator': hex(locator), 'type_descriptor': hex(descriptor),
                'decorated_name': name.decode('ascii')}
    camera_type = rtti(0xFB8B4C, b'.?AVSWGameCamera@Ronin@@')
    player_type = rtti(0xFBA660, b'.?AVPlayerSith@Ronin@@')
    rows = []

    def add(name, address, original, replacement, domain):
        assert pe.get_data(address - pe.OPTIONAL_HEADER.ImageBase, len(original)) == original, (name, hex(address))
        decoded = list(decoder.disasm(original, address))
        assert sum(i.size for i in decoded) == len(original), name
        for row in rows:
            assert address + len(original) <= row['address'] or row['address'] + row['bytes'] <= address, name
        rows.append({'name': name, 'address': address, 'bytes': len(original),
                     'original': original.hex(), 'replacement': replacement,
                     'original_instructions': [f'{i.mnemonic} {i.op_str}'.strip() for i in decoded],
                     'clock_domain': domain})
        row = rows[-1]
        if name in ('dmm_contact_interval_a','dmm_contact_interval_b'):
            assert original[-1]==30 and replacement==(original[:-1]+bytes([61])).hex()
            row['clock_domain']='accepted presentation-frame counter'
            row['fixed30_simulation_action']='retain_presentation_counter_compensation'
            row['fixed30_simulation_bytes']=replacement
            row['reason']='C69560 compares object+0x218 stamps against1240464, incremented by accepted presentation frames. Keep strict >61 while this counter remains60Hz; a30Hz simulation gate alone does not restore its rate.'
        elif domain in ('simulation cadence compensation', 'simulation smoothing'):
            row['fixed30_simulation_action'] = 'restore_original'
            row['fixed30_simulation_bytes'] = original.hex()
            row['reason'] = 'Original update cadence is restored; retaining native60 compensation would apply the rate correction twice.'
        elif domain == 'frame cap':
            row['fixed30_simulation_action'] = 'retain_render_cap_patch'
            row['reason'] = 'Presentation remains at60; the simulation gate supplies30 updates.'
        elif name.startswith('input_timing['):
            row['fixed30_simulation_action'] = 'restore_original_for_gated_player_path'
            row['fixed30_simulation_bytes'] = original.hex()
            row['reason'] = 'Registered PlayerSith virtual+0x14 is dispatched by C32E70 inside the main update. Restore native mouse/filter behavior when this ordinary path is gated to30; alternative dispatches still require review.'
        elif name.startswith('camera_smoothing['):
            row['fixed30_simulation_action'] = 'restore_original_for_gated_camera_path'
            row['fixed30_simulation_bytes'] = original.hex()
            row['reason'] = 'Application initialization constructs SWGameCamera into app+0x90, then main update dispatches its camera processing. Restore native coefficients when this ordinary path is gated to30; alternate wrappers remain under review.'
        else:
            row['fixed30_simulation_action'] = 'unresolved_update_ownership'
            row['reason'] = 'Determine whether this consumer executes at simulation or presentation cadence before selecting original/helper code.'

    for name, address, original, replacement in FrameRateFix.PATCHES:
        add(name, address, original, replacement.hex(),
            'frame cap' if name == 'cinematic_limit' else 'simulation cadence compensation')
    for module in FrameRateFix.HELPERS:
        short = module.__name__.rsplit('.', 1)[-1]
        domain = 'simulation smoothing' if short == 'physics_smoothing' else 'camera/input update cadence'
        for index, site in enumerate(module.SITES):
            add(f'{short}[{index}]', site[0], site[1], 'allocated CALL helper', domain)
    files = ['swtfu/tfu1/fps/profile.py', 'swtfu/tfu1/fps/physics_smoothing.py', 'swtfu/tfu1/fps/camera_smoothing.py', 'swtfu/tfu1/fps/input_timing.py', 'swtfu/tfu1/fps/update_rate.py']
    result = {
        'source_sha256': digest,
        'audit_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'profile_sources': {name: hashlib.sha256((ROOT.parent / name).read_bytes()).hexdigest() for name in files},
        'sites': sorted(rows, key=lambda row: row['address']),
        'site_count': len(rows),
        'original_instruction_boundaries_and_nonoverlap_validated': True,
        'integration_requirement': 'Reconcile each simulation/camera cadence compensation before switching to fixed30 simulation with render60. Do not layer the new scheduler blindly over native60.',
        'fixed30_migration': {
            'restore_original_sites': [r['name'] for r in rows if r['fixed30_simulation_action'] == 'restore_original'],
            'retain_presentation_counter_sites': [r['name'] for r in rows if r['fixed30_simulation_action'] == 'retain_presentation_counter_compensation'],
            'unresolved_sites': [r['name'] for r in rows if r['fixed30_simulation_action'] == 'unresolved_update_ownership'],
            'player_path_restore_sites': [r['name'] for r in rows if r['fixed30_simulation_action'] == 'restore_original_for_gated_player_path'],
            'camera_path_restore_sites': [r['name'] for r in rows if r['fixed30_simulation_action'] == 'restore_original_for_gated_camera_path'],
            'ready_to_apply': False,
            'input_retention_required': True,
            'input_retention_evidence': {
                'axis_object': 'Input context+0xCE0',
                'player_reads': ['0x69DF66: axis+8', '0x69DF76: axis+0xC'],
                'publisher_overwrites': ['0x6B8E0E: axis+8', '0x6B8E24: axis+0xC'],
                'constraint': 'Retain raw mouse deltas and transient button transitions across presentation-only polls before gating player updates. Do not sum mapped digital/analog axes indiscriminately.',
                'live_poll_to_publisher_chain_fully_proven': False,
                'retention_implemented': False,
            },
            'writes_game_or_process': False,
            'alternate_camera_wrapper_references': wrapper_references(pe),
            'camera_input_caller_evidence': ownership_evidence,
            'camera_vtable_evidence': {'table': '0xFB8B4C', 'slot': '0x3C',
                                       'target': '0x759080',
                                       'rtti': camera_type,
                                       'writes': ['0x74DC2D', '0x755990'],
                                       'live_receiver_identity_proven': False},
            'input_vtable_evidence': {'table': '0xFBA660', 'slot': '0x14',
                                      'target': '0x7C2200',
                                      'rtti': player_type,
                                      'writes': ['0x7C07DE', '0x7C13FB'],
                                      'caller_cadence_proven': False},
            'open_dispatch_edges': [
                'Ordinary app+0x90 camera creation resolves to SWGameCamera; review alternate wrappers408B20/410770 and lifetime/replacement paths.',
                'Registered PlayerSith path resolves through C32E70; review alternate dispatch/lifetime paths before global input-hook migration.',
                'Alternative callers and modes of camera smoothing must retain their native cadence.',
            ],
        },
        'scope': 'Offline source/code inventory only; no process inspection, timing measurement or physics equivalence proof.',
    }
    (ROOT.parent / 'analysis' / 'runtime-profile-audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'Validated {len(rows)} current native60 patch sites against decoded source: complete instructions, no overlapping writes.')


if __name__ == '__main__':
    main()
