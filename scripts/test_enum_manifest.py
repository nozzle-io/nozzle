import unittest

from generate_enum_manifest import read_enums


class EnumManifestTests(unittest.TestCase):
    def test_literal_alias_and_shift(self):
        header = """typedef enum NozzleExample {
NOZZLE_ZERO = 0,
NOZZLE_ALIAS = 0,
NOZZLE_FLAG = 1u << 3,
} NozzleExample;"""
        self.assertEqual(read_enums(header), {"NozzleExample": {
            "NOZZLE_ZERO": 0, "NOZZLE_ALIAS": 0, "NOZZLE_FLAG": 8}})

    def test_unfamiliar_member_is_not_silently_skipped(self):
        for member in ["NOZZLE_IMPLICIT,", "NOZZLE_EXPR = function(),", "#if FEATURE"]:
            with self.subTest(member=member), self.assertRaises(ValueError):
                read_enums("typedef enum NozzleExample {\n" + member + "\n} NozzleExample;")

    def test_missing_or_unterminated_enum_is_not_empty_success(self):
        for header in ["", "typedef enum NozzleExample {\nNOZZLE_ZERO = 0,"]:
            with self.subTest(header=header), self.assertRaises(ValueError):
                read_enums(header)


if __name__ == "__main__":
    unittest.main()
