"""Unit tests for the artifact verifier, not the Chromium acceptance itself."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
import zipfile
from PIL import Image

spec=importlib.util.spec_from_file_location("bucket_artifact_evidence",Path(__file__).with_name("bucket_artifact_evidence.py"))
acceptance=importlib.util.module_from_spec(spec);spec.loader.exec_module(acceptance)

class Evidence(unittest.TestCase):
    def write_pair(self,root,color=(192,32,176,255),merged=(192,32,176,255)):
        png=Path(root)/"painting.png";ora=Path(root)/"painting.ora"
        Image.new("RGBA",(4,3),merged).save(png)
        def image_bytes(value):
            buffer=io.BytesIO();Image.new("RGBA",(4,3),value).save(buffer,format="PNG");return buffer.getvalue()
        with zipfile.ZipFile(ora,"w") as z:
            z.writestr("mimetype",b"image/openraster")
            z.writestr("mergedimage.png",image_bytes(merged))
            z.writestr("stack.xml",'<image w="4" h="3"><stack><layer name="Color fill" src="data/fill.png"/><layer name="Ink" src="data/ink.png"/></stack></image>')
            z.writestr("data/fill.png",image_bytes(color));z.writestr("data/ink.png",image_bytes((0,0,0,0)))
        return ora,png
    def test_verifier_present(self):
        self.assertTrue(hasattr(acceptance,"verify_bucket_artifacts"))
    def test_valid_native_layer_pixels(self):
        with tempfile.TemporaryDirectory() as root:
            ora,png=self.write_pair(root)
            result=acceptance.verify_bucket_artifacts(ora,png,2,(4,3))
            self.assertEqual(result["layers"],2)
            self.assertEqual(result["fill_pixels"],12)
    def test_wrong_count_and_pixel_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            ora,png=self.write_pair(root)
            with self.assertRaises(AssertionError):acceptance.verify_bucket_artifacts(ora,png,3,(4,3))
            ora,png=self.write_pair(root,color=(1,2,3,255))
            with self.assertRaises(AssertionError):acceptance.verify_bucket_artifacts(ora,png,2,(4,3))
if __name__=="__main__":unittest.main()
