import unittest

import c89ize


class C89izeTransformTests(unittest.TestCase):
    def test_leading_declarations_after_comment_stay_in_place(self):
        source = (
            "void f(void) {\n"
            "\t/* declarations are still at block start */\n"
            "\tint first = 1;\n"
            "\tint second = 2;\n"
            "\tuse(first, second);\n"
            "}\n"
        )
        transformed, changes = c89ize.transform(source)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, source)

    def test_function_header_does_not_hide_mid_block_declaration_run(self):
        source = (
            "void f(void) {\n"
            "\tuse();\n"
            "\tint first = 1;\n"
            "\tcss_unit second = CSS_UNIT_PX;\n"
            "\tuse_values(first, second);\n"
            "}\n"
        )
        expected = (
            "void f(void) {\n"
            "\tint first;\n"
            "\tcss_unit second;\n"
            "\tuse();\n"
            "\tfirst = 1;\n"
            "\tsecond = CSS_UNIT_PX;\n"
            "\tuse_values(first, second);\n"
            "}\n"
        )
        transformed, changes = c89ize.transform(source)
        self.assertEqual(changes, 2)
        self.assertEqual(transformed, expected)

    def test_declaration_after_multiline_initializer_stays_in_place(self):
        source = (
            "void f(void) {\n"
            "\tstruct box *first = make_box(\n"
            "\t\tcontext);\n"
            "\tstruct box *second;\n"
            "\tuse(first, second);\n"
            "}\n"
        )
        transformed, changes = c89ize.transform(source)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, source)

    def test_aggregate_fields_are_never_reordered(self):
        source = (
            "typedef struct sample {\n"
            "\tint first;\n"
            "\tvoid (*callback)(void);\n"
            "\tint last;\n"
            "} sample;\n"
        )
        transformed, changes = c89ize.transform(source)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, source)


if __name__ == "__main__":
    unittest.main()
