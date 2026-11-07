import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("automatic_stages", ROOT / "tools/experimental/automatic_stages/plan.py")
planner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(planner)
CONTRACT_SPEC=importlib.util.spec_from_file_location("automatic_contract",ROOT/"tools/experimental/automatic_stages/contract.py")
contract=importlib.util.module_from_spec(CONTRACT_SPEC);CONTRACT_SPEC.loader.exec_module(contract)


def canvas(size=40, color=(230, 160, 80, 255)):
    image = np.empty((size, size, 4), np.uint8)
    image[:] = color
    return image


class AutomaticStagesTests(unittest.TestCase):
    def test_uniform_source_has_only_a_real_base_phase(self):
        image = canvas()
        result = planner.plan(image, [np.full((40, 40), 255, np.uint8)])
        self.assertTrue(result["strokes"])
        self.assertLess(len(result["strokes"]), 20)
        self.assertEqual({s["phase"] for s in result["strokes"]}, {"base"})

    def test_scene_order_is_global_across_objects(self):
        image = canvas(64)
        image[12:25, 4:24] = (255, 220, 180, 255)
        image[30:35, 38:57] = (255, 220, 180, 255)
        image[18:21, 8:23] = (20, 20, 20, 255)
        image[33:36, 40:56] = (20, 20, 20, 255)
        left = np.zeros((64, 64), np.uint8);left[:, :32] = 255
        result = planner.plan(image, [left, 255-left])
        order = [planner.PHASES.index(s["phase"]) for s in result["strokes"]]
        self.assertEqual(order, sorted(order))
        self.assertEqual(set(order), {0, 1, 2})
        self.assertEqual(result, planner.plan(image, [left, 255-left]))

    def test_diagonal_ink_is_a_line_not_many_pixel_dabs(self):
        mask = np.eye(32, dtype=bool)
        strokes = planner.structure_paths(mask, np.array([0., 0., 0., 1.]), reviewed_mask=np.ones_like(mask))
        self.assertEqual(len(strokes), 1)
        path = strokes[0]["geometry"]
        self.assertGreater(sum(math.hypot(b[0]-a[0], b[1]-a[1]) for a,b in zip(path,path[1:])), 40)

    def test_genuine_highcontrast_isolated_dot_survives(self):
        image = canvas(32, (245, 245, 245, 255));image[16, 16] = (0, 0, 0, 255)
        result = planner.plan(image, [np.full((32, 32), 255, np.uint8)])
        ink = [s for s in result["strokes"] if s["phase"] == "detail"]
        self.assertEqual(len(ink), 1)
        self.assertEqual(ink[0]["geometry"][0][:2], (16.5, 16.5))

    def test_lowcontrast_noise_does_not_acquire_pixel_events(self):
        image = canvas(40, (230, 230, 230, 255))
        for y in range(3, 40, 6):
            for x in range(3, 40, 6):image[y, x] = (230, 205, 215, 255)
        result = planner.plan(image, [np.full((40, 40), 255, np.uint8)])
        self.assertLess(len(result["strokes"]), 20)
        self.assertEqual({s["phase"] for s in result["strokes"]}, {"base"})

    def test_gray_edges_cannot_borrow_remote_accent_pigment(self):
        image = canvas(64, (242, 163, 72, 255))
        image[2:7, 2:7] = (57, 125, 130, 255)
        image[28:31, 28:47] = (66, 50, 44, 255)
        image[31, 28:47] = (145, 125, 100, 255)
        centers = np.array([[242,163,72,255], [66,50,44,255], [57,125,130,255]], dtype=float)
        labels = planner.local_assignment(image, np.ones((64,64), bool), centers).reshape(64,64)
        self.assertTrue(np.all(labels[31,28:47] != 2))
        self.assertTrue(np.all(labels[2:7,2:7] == 2))

    def test_masks_and_budgets_are_not_relaxed(self):
        image = canvas();mask = np.full((40,40),255,np.uint8)
        with self.assertRaisesRegex(ValueError,"overlapping"):
            planner.plan(image,[mask,mask])
        bad=mask.copy();bad[0,0]=128
        with self.assertRaisesRegex(ValueError,"binary"):
            planner.plan(image,[bad])
        with self.assertRaisesRegex(ValueError,"event budget"):
            planner.plan(image,[mask],max_events=1)
        with self.assertRaisesRegex(ValueError,"sample budget"):
            planner.plan(image,[mask],max_samples=1)

    def test_translation_does_not_change_the_plan(self):
        image=canvas(64);mask=np.zeros((64,64),np.uint8);mask[8:28,8:28]=255
        image[12:16,12:23]=(0,0,0,255)
        one=planner.plan(image,[mask])
        shifted=canvas(64);shifted[7:,13:]=image[:-7,:-13]
        moved=np.zeros_like(mask);moved[7:,13:]=mask[:-7,:-13]
        two=planner.plan(shifted,[moved])
        self.assertEqual(len(one["strokes"]),len(two["strokes"]))
        for a,b in zip(one["strokes"],two["strokes"]):
            self.assertEqual(a["phase"],b["phase"])
            self.assertEqual(a["color"],b["color"])
            self.assertEqual(a["geometry"],[(x-13,y-7,p) for x,y,p in b["geometry"]])

    def test_native_roundtrip_and_rejection(self):
        configured=os.environ.get("DRAWBORAD_NATIVE_PLAN")
        runner=Path(configured) if configured is not None else ROOT/"local-build/staged/native-plan"
        if configured is not None:
            self.assertTrue(configured and runner.is_file(),
                            "DRAWBORAD_NATIVE_PLAN was explicitly set but is not a runner file")
        elif not runner.is_file():
            self.skipTest("native prototype must be built first")
        image=canvas(24);image[9:12,5:19]=(0,0,0,255);mask=np.full((24,24),255,np.uint8)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image.tofile(root/"source.rgba");mask.tofile(root/"mask.u8")
            planner.write_plan(planner.plan(image,[mask]),root/"plan")
            args=[str(runner),str(root/"plan/plan.txt"),str(root/"source.rgba"),str(root/"native"),str(root/"mask.u8")]
            run=subprocess.run(args,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            audit=json.loads((root/"native/audit.json").read_text())
            self.assertTrue(audit["savedReopenedVerified"])
            self.assertEqual(audit["sourceAssetReads"],0)
            self.assertEqual(set(audit["resourceKinds"]),{"Mask"})
            self.assertTrue(all(layer["nonempty"] for layer in audit["layers"]))
            self.assertTrue(np.all(np.fromfile(root/"native/final.rgba",np.uint8)[3::4]==255))
            self.assertTrue(np.all(np.fromfile(root/"native/zero.rgba",np.uint8)==0))
            # No clobber, and changed reviewed masks cannot license old paths.
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            mask[:]=0;mask[0,0]=255;mask.tofile(root/"mask.u8")
            args[3]=str(root/"invalid")
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertFalse((root/"invalid").exists())
            mask[:]=255;mask.tofile(root/"mask.u8")
            with (root/"source.rgba").open("ab") as f:f.write(b"unexpected trailing data")
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertFalse((root/"invalid").exists())

    def test_native_directory_created_during_planning_is_not_clobbered(self):
        configured=os.environ.get("DRAWBORAD_NATIVE_PLAN")
        runner=Path(configured) if configured is not None else ROOT/"local-build/staged/native-plan"
        if configured is not None:
            self.assertTrue(configured and runner.is_file(),"DRAWBORAD_NATIVE_PLAN must name a runner file")
        elif not runner.is_file():self.skipTest("native prototype must be built first")
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);np.full((24,24,4),255,np.uint8).tofile(root/"source.rgba")
            np.full((24,24),255,np.uint8).tofile(root/"mask.u8")
            (root/"plan.txt").write_text("DBPAINT1 24 24 1 1\n0 0 1 .3 .5 .7 1 100000\n"+"8.5 8.5 1\n"*100000)
            args=[str(runner),str(root/"plan.txt"),str(root/"source.rgba"),str(root/"native"),str(root/"mask.u8")]
            process=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            time.sleep(.005)
            (root/"native").mkdir()
            sentinel=root/"native/final.rgba";sentinel.write_bytes(b"existing owner data")
            stdout,stderr=process.communicate(timeout=30)
            self.assertEqual(process.returncode,1,stdout+stderr)
            self.assertEqual(sentinel.read_bytes(),b"existing owner data")
            self.assertFalse((root/"native/automatic.draw").exists())

    def test_explicit_missing_native_runner_fails_instead_of_skipping(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in (str(Path(directory)/"missing-runner"), directory, ""):
                with self.subTest(runner=value), mock.patch.dict(os.environ, {"DRAWBORAD_NATIVE_PLAN": value}):
                    result=unittest.TestResult()
                    AutomaticStagesTests("test_native_roundtrip_and_rejection").run(result)
                    self.assertEqual(len(result.failures),1)
                    self.assertEqual(result.errors,[])
                    self.assertEqual(result.skipped,[])
                    self.assertIn("DRAWBORAD_NATIVE_PLAN",result.failures[0][1])

    def test_evaluation_rejects_empty_or_invalid_manifest_before_execution(self):
        fixture=dict(id="example",width=24,height=24,rgba="source.rgba",masks=["mask.u8"])
        cases=[None,[],{},dict(fixtures=[]),dict(fixtures={}),dict(fixtures=[None]),
               dict(fixtures=[{}]),dict(fixtures=[fixture,fixture])]
        for key,value in [("id","../escape"),("id",7),("width",0),("width",True),
                          ("height",513),("height","24"),("rgba",None),("rgba",""),
                          ("masks",[]),("masks","mask.u8"),("masks",[""]),
                          ("masks",["mask.u8"]*17)]:
            cases.append(dict(fixtures=[dict(fixture,**{key:value})]))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for index,payload in enumerate([json.dumps(value) for value in cases]+["{broken"]):
                with self.subTest(manifest=payload):
                    manifest=root/"manifest.json";manifest.write_text(payload)
                    output=root/f"out-{index}"
                    run=subprocess.run([sys.executable,str(ROOT/"tools/experimental/automatic_stages/run_evaluation.py"),
                                        "--manifest",str(manifest),"--native-runner",str(root/"missing-runner"),
                                        "--output",str(output)],capture_output=True,text=True)
                    self.assertEqual(run.returncode,2,run.stdout+run.stderr)
                    self.assertIn("invalid manifest",run.stderr)
                    self.assertNotIn("Traceback",run.stderr)
                    self.assertNotIn("EXECUTED",run.stdout)
                    self.assertFalse(output.exists())
                    self.assertEqual(manifest.read_text(),payload)
            # A structurally valid manifest reaches execution. A soft-alpha fixture
            # is then reported as a preflight failure, not as a malformed manifest.
            image=canvas(24);image[0,0,3]=128;image.tofile(root/"source.rgba")
            np.full((24,24),255,np.uint8).tofile(root/"mask.u8")
            manifest.write_text(json.dumps(dict(fixtures=[fixture])))
            output=root/"valid-manifest"
            run=subprocess.run([sys.executable,str(ROOT/"tools/experimental/automatic_stages/run_evaluation.py"),
                                "--manifest",str(manifest),"--native-runner",str(root/"missing-runner"),
                                "--output",str(output)],capture_output=True,text=True)
            self.assertEqual(run.returncode,1,run.stdout+run.stderr)
            report=json.loads((output/"runs.json").read_text())
            self.assertEqual(report["runs"],[])
            self.assertEqual([item["fixture"] for item in report["failures"]],["example"])
            self.assertIn("execution-only",report["scope"])
            self.assertFalse((output/"example-plan-1/plan.txt").exists())
            self.assertFalse((output/"example-native-1").exists())

    def test_contract_rejects_soft_alpha_distributed_color_and_gradients(self):
        image=canvas(40);mask=np.full((40,40),255,np.uint8)
        self.assertTrue(contract.preflight(image,[mask])["eligible"])
        soft=image.copy();soft[0,0,3]=128
        self.assertIn("object-0:nonopaque-reviewed-pixels",contract.preflight(soft,[mask])["rejectionReasons"])
        photo=np.random.default_rng(773).integers(0,256,(40,40,4),dtype=np.uint8);photo[:,:,3]=255
        self.assertIn("object-0:palette-too-distributed",contract.preflight(photo,[mask])["rejectionReasons"])
        gradient=canvas(40)
        for x in range(40):gradient[:,x,:3]=x*4
        self.assertIn("object-0:smooth-gradient-content",contract.preflight(gradient,[mask])["rejectionReasons"])

    def test_contract_rejects_malformed_arrays_before_analysis(self):
        image=canvas(24);mask=np.full((24,24),255,np.uint8)
        cases=[(np.empty((0,24,4),np.uint8),[np.empty((0,24),np.uint8)]),
               (image[:,:,:3],[mask]),(image.astype(float),[mask]),(image,[mask[:,:12]]),
               (image,[mask,mask]),(image,[np.full((24,24),128,np.uint8)]),(image,[])]
        for source,masks in cases:
            with self.subTest(shape=source.shape,masks=len(masks)):
                report=contract.preflight(source,masks)
                self.assertFalse(report["eligible"])
                self.assertTrue(report["rejectionReasons"])

    def test_cli_requires_optin_and_does_not_silently_process_unsupported_alpha(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image=canvas(24);image[5,5,3]=128;image.tofile(root/"source.rgba")
            np.full((24,24),255,np.uint8).tofile(root/"mask.u8")
            args=[sys.executable,str(ROOT/"tools/experimental/automatic_stages/plan.py"),"--rgba",str(root/"source.rgba"),
                  "--width","24","--height","24","--mask",str(root/"mask.u8"),"--output",str(root/"out")]
            self.assertEqual(subprocess.run(args,capture_output=True).returncode,2)
            self.assertFalse((root/"out").exists())
            self.assertEqual(subprocess.run(args+["--experimental"],capture_output=True).returncode,3)
            self.assertFalse((root/"out/plan.txt").exists())
            report=json.loads((root/"out/preflight.json").read_text())
            self.assertFalse(report["eligible"])
            args[args.index("--output")+1]=str(root/"oversized")
            original=(root/"source.rgba").read_bytes()
            (root/"source.rgba").write_bytes(original+b"extra")
            self.assertNotEqual(subprocess.run(args+["--experimental"],capture_output=True).returncode,0)
            self.assertFalse((root/"oversized").exists())
            self.assertEqual((root/"source.rgba").read_bytes(),original+b"extra")
            args[args.index("--width")+1]="513"
            invalid=subprocess.run(args+["--experimental"],capture_output=True,text=True)
            self.assertEqual(invalid.returncode,2)
            self.assertIn("dimension/object budget",invalid.stderr)
            self.assertFalse((root/"oversized").exists())


if __name__ == "__main__":unittest.main()
