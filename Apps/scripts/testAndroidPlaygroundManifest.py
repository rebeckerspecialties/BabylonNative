"""Check the source manifest, or: apkanalyzer manifest print APP.apk | python3 THIS_FILE --stdin."""

import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


MANIFEST = Path(__file__).resolve().parents[1] / "Playground/Android/app/src/main/AndroidManifest.xml"
A = "{http://schemas.android.com/apk/res/android}"


class PlaygroundManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = ET.parse(MANIFEST).getroot()
        activities = cls.root.findall("application/activity")
        cls.activity, = [a for a in activities if a.get(A + "name") in (
            "PlaygroundActivity", ".PlaygroundActivity", "com.android.babylonnative.playground.PlaygroundActivity"
        )]

    def filters(self, action):
        return [f for f in self.activity.findall("intent-filter")
                if any(a.get(A + "name") == action for a in f.findall("action"))]

    def test_flat_and_vr_launchers(self):
        self.assertEqual(self.activity.get(A + "exported"), "true")
        required = {"android.intent.category.LAUNCHER", "com.oculus.intent.category.VR"}
        self.assertTrue(any(required <= {c.get(A + "name") for c in f.findall("category")}
                            for f in self.filters("android.intent.action.MAIN")))

    def test_openxr_minimum_and_optional_passthrough(self):
        def feature(name):
            matches = self.root.findall(f"uses-feature[@{A}name='{name}']")
            self.assertEqual(len(matches), 1, name)
            return matches[0]

        openxr = feature("android.software.xr.api.openxr")
        self.assertEqual(openxr.get(A + "required"), "true")
        self.assertEqual(int(openxr.attrib[A + "version"], 0), 0x00010000)
        self.assertEqual(feature("com.oculus.feature.PASSTHROUGH").get(A + "required"), "false")

    def test_glb_view_intents(self):
        schemes = set()
        for f in self.filters("android.intent.action.VIEW"):
            categories = {c.get(A + "name") for c in f.findall("category")}
            data = f.findall("data")
            # AAPT unescapes the backslash in the source XML's path pattern.
            if "android.intent.category.DEFAULT" not in categories or not any(
                d.get(A + "pathPattern") in (r".*\\.glb", r".*\.glb") for d in data
            ):
                continue
            for d in data:
                scheme = d.get(A + "scheme")
                if d.get(A + "host") == "*" and (scheme != "content" or
                        any(x.get(A + "mimeType") == "*/*" for x in data)):
                    schemes.add(scheme)
        self.assertLessEqual({"file", "content"}, schemes)

    def test_other_graphics_features(self):
        gles, = self.root.findall(f"uses-feature[@{A}glEsVersion]")
        self.assertEqual(int(gles.attrib[A + "glEsVersion"], 0), 0x00030000)
        self.assertEqual(gles.get(A + "required"), "true")
        arcore = self.root.find(f"application/meta-data[@{A}name='com.google.ar.core']")
        self.assertIsNotNone(arcore)
        self.assertEqual(arcore.get(A + "value"), "optional")


if __name__ == "__main__":
    if "--stdin" in sys.argv:
        sys.argv.remove("--stdin")
        MANIFEST = sys.stdin.buffer
    unittest.main()
