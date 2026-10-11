import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "tabletop_library", Path(__file__).parents[1] / "tools" / "prepare_tabletop_library.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TabletopLibrary(unittest.TestCase):
    def test_custom_right_foot_and_bounded_return(self):
        neutral = [90, 90, 90, 31]
        library = module.build_library(neutral)
        self.assertEqual(len(library), 6)
        for sequence in library.values():
            self.assertEqual(sequence["frames"][0]["angles"], neutral)
            self.assertEqual(sequence["frames"][-1]["angles"], neutral)
            self.assertLessEqual(len(sequence["frames"]), 16)
            for frame in sequence["frames"]:
                self.assertTrue(all(abs(a - n) <= 1 for a, n in zip(frame["angles"], neutral)))

    def test_servo_boundary_is_rejected(self):
        with self.assertRaises(ValueError):
            module.build_library([90, 90, 90, 0])


if __name__ == "__main__":
    unittest.main()
