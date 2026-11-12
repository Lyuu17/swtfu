"""Cinematic profile transitions and saved-state lifetime regression checks."""
import ctypes as C
import struct
import unittest
from unittest.mock import patch
from swtfu.tfu1.fps import profile as fix


class Game:
    def __init__(self, fps=30, cinematic=True):
        self.context, self.handle, self.stopped = 0x1000, None, False
        self.code = fix.ORIGINAL
        self.codes = {address: original for _, address, original, patched in fix.PATCHES}
        for module in fix.HELPERS:
            self.codes.update({site[0]:site[1] for site in module.SITES})
            self.codes.update(getattr(module,'SOURCES',()))
        self.values = {self.context + 0x150: fps, fix.CINEMATIC_STATE: 0x2000 if cinematic else 0,
                       0x2000 + fix.SAVED_LIMIT_OFFSET: fps}
        self.kernel = self

    def read(self, address, size):
        return self.codes[address]

    def u32(self, address):
        return self.values[address]

    def f32(self, address):
        return self.values[address]

    def set_limit(self, expected, target):
        assert self.stopped and self.values[self.context + 0x150] == expected
        changed = expected != target
        self.values[self.context + 0x150] = target
        return changed

    def WriteProcessMemory(self, handle, address, data, size, count):
        assert self.stopped, "Saved cinematic state must not be written while it can be freed"
        self.values[address], = struct.unpack("<f", data.raw[:4])
        C.cast(count, C.POINTER(C.c_size_t))[0] = 4
        return True


class Freeze:
    def __init__(self, game, *args, **kwargs):
        self.game = game

    def __enter__(self):
        self.game.stopped = True
        if getattr(self.game, "change_object", False):
            self.game.values[fix.CINEMATIC_STATE] = 0x3000
            self.game.values[0x3000 + fix.SAVED_LIMIT_OFFSET] = 30

    def __exit__(self, *args):
        self.game.stopped = False


def apply(game, address, original, replacement):
    if game.codes[address] == replacement:
        return False
    assert game.codes[address] == original
    game.codes[address] = replacement
    if address == fix.ADDRESS:
        game.code = replacement
    return True


@patch.object(fix, "apply_code_patch", apply)
@patch.object(fix, "FrozenThreads", Freeze)
@patch.object(fix.physics_smoothing, "set_enabled", lambda game, enabled: False)
@patch.object(fix.camera_smoothing, "set_enabled", lambda game, enabled: False)
@patch.object(fix.input_timing, "set_enabled", lambda game, enabled: False)
@patch.object(fix.update_rate, "set_enabled", lambda game, enabled: False)
class ProfileTests(unittest.TestCase):
    def test_unsupported_input_accessor_refused_before_mutation(self):
        game=Game();address=fix.input_timing.SOURCES[0][0]
        game.codes[address]=b'foreign';before=dict(game.codes)
        with self.assertRaisesRegex(RuntimeError,'Unsupported raw mouse'):
            fix.set_profile(game,60)
        self.assertEqual(game.codes,before)

    def test_all_fixed_sites_match_supported_image(self):
        import pefile
        pe=pefile.PE('analysis/SWTFU.decoded.exe')
        for name,address,original,patched in fix.PATCHES:
            self.assertEqual(pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,len(original)),original,name)
            self.assertEqual(len(original),len(patched),name)

    def test_late_helper_failure_rolls_back_fixed_sites(self):
        game = Game()
        before = dict(game.codes)
        with patch.object(fix.camera_smoothing,"set_enabled",side_effect=OSError("injected helper failure")):
            with self.assertRaisesRegex(RuntimeError,"prior profile restored"):
                fix.set_profile(game,60)
        self.assertEqual(game.codes,before)
        self.assertEqual(game.values[game.context+0x150],30)
        self.assertFalse(game.stopped)

    def test_cap_failure_restores_cinematic_and_code(self):
        game = Game()
        before = dict(game.codes)
        def fail(expected,target):
            game.values[game.context+0x150]=target
            raise OSError("injected post-write failure")
        game.set_limit=fail
        with self.assertRaisesRegex(RuntimeError,"prior profile restored"):
            fix.set_profile(game,60)
        self.assertEqual(game.codes,before)
        self.assertEqual(game.values[game.context+0x150],30)
        self.assertEqual(game.values[0x2000+fix.SAVED_LIMIT_OFFSET],30)
        self.assertFalse(game.stopped)

    def test_failed_restore_preserves_prior_60_profile(self):
        game = Game()
        fix.set_profile(game,60)
        before = dict(game.codes)
        with patch.object(fix.camera_smoothing,"set_enabled",side_effect=OSError("injected restore failure")):
            with self.assertRaisesRegex(RuntimeError,"prior profile restored"):
                fix.set_profile(game,30)
        self.assertEqual(game.codes,before)
        self.assertEqual(game.values[game.context+0x150],60)

    def test_incomplete_rollback_reports_restart(self):
        game = Game()
        real_write=game.WriteProcessMemory
        def write(handle,address,data,size,count):
            if struct.unpack('<f',data.raw[:4])[0]==30:
                raise OSError('injected rollback write failure')
            return real_write(handle,address,data,size,count)
        def fail(expected,target):
            game.values[game.context+0x150]=target
            raise OSError('injected cap verification failure')
        game.WriteProcessMemory=write;game.set_limit=fail
        with self.assertRaisesRegex(RuntimeError,'rollback incomplete:.*Restart the game'):
            fix.set_profile(game,60)
        self.assertFalse(game.stopped)

    def test_foreign_code_is_not_overwritten_during_rollback(self):
        game = Game()
        address=fix.PATCHES[1][1]
        def fail(target,enabled):
            target.codes[address]=b'foreign'
            raise OSError('injected concurrent code change')
        with patch.object(fix.camera_smoothing,'set_enabled',fail):
            with self.assertRaisesRegex(RuntimeError,'rollback incomplete'):
                fix.set_profile(game,60)
        self.assertEqual(game.codes[address],b'foreign')
        self.assertFalse(game.stopped)

    def test_unsupported_camera_refused_before_any_mutation(self):
        game = Game()
        with patch.object(fix.camera_smoothing, "validate", side_effect=RuntimeError("Unsupported camera")):
            with self.assertRaisesRegex(RuntimeError, "Unsupported camera"):
                fix.set_profile(game, 60)
        self.assertTrue(all(game.codes[address] == original for _, address, original, _ in fix.PATCHES))
        self.assertEqual(game.values[game.context+0x150],30)

    def test_apply_restore_during_cinematic(self):
        game = Game()
        self.assertTrue(fix.set_profile(game, 60))
        self.assertEqual(game.values[0x2000 + fix.SAVED_LIMIT_OFFSET], 60)
        self.assertEqual(game.code, fix.PATCHED if not fix.KEEP_ORIGINAL else fix.ORIGINAL)
        self.assertTrue(all(game.codes[address] == (original if name in fix.KEEP_ORIGINAL else patched)
                            for name, address, original, patched in fix.PATCHES))
        self.assertFalse(fix.set_profile(game, 60))
        self.assertTrue(fix.set_profile(game, 30))
        self.assertEqual(game.values[0x2000 + fix.SAVED_LIMIT_OFFSET], 30)
        self.assertEqual(game.code, fix.ORIGINAL)
        self.assertTrue(all(game.codes[address] == original for _, address, original, patched in fix.PATCHES))
        self.assertFalse(fix.set_profile(game, 30))

    def test_old_cinematic_patch_is_restored_at_60(self):
        game = Game()
        game.code = game.codes[fix.ADDRESS] = fix.PATCHED  # session patched by an older helper
        self.assertTrue(fix.set_profile(game, 60))
        self.assertEqual(game.code, fix.PATCHED if not fix.KEEP_ORIGINAL else fix.ORIGINAL)

    def test_new_saved_object_is_used_after_freeze(self):
        game = Game()
        game.change_object = True
        fix.set_profile(game, 60)
        self.assertEqual(game.values[0x2000 + fix.SAVED_LIMIT_OFFSET], 30)
        self.assertEqual(game.values[0x3000 + fix.SAVED_LIMIT_OFFSET], 60)

    def test_no_cinematic(self):
        game = Game(cinematic=False)
        fix.set_profile(game, 60)
        self.assertEqual(game.values[game.context + 0x150], 60)

    def test_unsupported_state_refused_before_mutation(self):
        game = Game(fps=45)
        with self.assertRaises(RuntimeError):
            fix.set_profile(game, 60)
        self.assertEqual(game.code, fix.ORIGINAL)

    def test_unsupported_later_site_refused_before_any_mutation(self):
        game = Game()
        game.codes[fix.PATCHES[-1][1]] = b"unsupported"
        before = dict(game.codes)
        with self.assertRaises(RuntimeError):
            fix.set_profile(game, 60)
        self.assertEqual(game.codes, before)


if __name__ == "__main__":
    unittest.main()
