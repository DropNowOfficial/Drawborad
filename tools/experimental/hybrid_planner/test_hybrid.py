import unittest
from unittest import mock
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import numpy as np
from scipy import ndimage as ndi
import hybrid

class HybridTests(unittest.TestCase):
    def test_local_error_energy_handles_roundoff_and_stays_finite(self):
        source=np.ones((32,32,4),float);baseline=np.full((32,32,4),255,np.uint8)
        rng=np.random.default_rng(0);baseline[4:12,4:20,:3]=rng.integers(80,220,(8,16,3),dtype=np.uint8)
        with np.errstate(invalid='raise'):
            regions=hybrid.correction_regions(source,[np.full((32,32),255,np.uint8)],baseline)
        self.assertTrue(regions)
        self.assertTrue(all(np.isfinite(region['value']) for region in regions))

    def test_antialias_mixture_is_not_an_isolated_color_mark(self):
        image=np.ones((5,5,4),float)*.9;image[:,:,3]=1;image[1,1,:3]=.1;image[2,2,:3]=.4
        region=np.zeros((5,5),bool);region[2,2]=True
        self.assertFalse(hybrid.isolated_source_mark(image,region,np.ones((5,5),bool)))

    def test_rare_chroma_mark_between_bright_and_dark_values_survives(self):
        image=np.ones((5,5,4),float);image[:,:,:3]=(.9,.8,.7);image[1,:3,:3]=.1;image[2,2,:3]=(.7,.25,.3)
        region=np.zeros((5,5),bool);region[2,2]=True
        self.assertTrue(hybrid.isolated_source_mark(image,region,np.ones((5,5),bool)))

    def test_small_color_shape_has_continuous_contour_candidate(self):
        region=np.zeros((8,8),bool);region[3,3]=region[3,4]=region[4,3]=True
        paths=hybrid.correction_paths(region,np.array([.7,.2,.3,1.]),np.ones((8,8),bool))
        self.assertTrue(any(hybrid.length(path)>=3 for path in paths))

    def test_rejected_antialias_seeds_do_not_starve_coherent_color_region(self):
        source=np.ones((96,96,4),float);baseline=np.full((96,96,4),255,np.uint8)
        for y in range(3,82,7):
            for x in range(3,82,7):
                source[y,x,:3]=.5;source[y-1,x-1,:3]=.1;baseline[y-1,x-1,:3]=26
        target=np.zeros((96,96),bool);target[90,90]=target[90,91]=target[91,90]=True
        source[target,:3]=(.7,.3,.4);baseline[target,:3]=(128,77,77)
        regions=hybrid.correction_regions(source,[np.full((96,96),255,np.uint8)],baseline)
        self.assertTrue(any(np.array_equal(r['mask'],target) for r in regions))
        self.assertLessEqual(len(regions),96)

class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.image=np.full((16,16,4),255,np.uint8)
        self.mask=np.full((16,16),255,np.uint8)
        self.runner=Path(os.environ.get('DRAWBORAD_HYBRID_NATIVE',str(Path(__file__).with_name('build')/'native-hybrid')))

    def test_programmatic_data_contract_rejects_before_planning(self):
        cases=[(None,[self.mask]),(self.image[:,:,:3],[self.mask]),
               (self.image.astype(float),[self.mask]),(np.zeros((0,16,4),np.uint8),[self.mask]),
               (self.image,[]),(self.image,[self.mask]*17),(self.image,[self.mask[:,:8]]),
               (self.image,[self.mask.astype(float)]),(self.image,[np.full((16,16),127,np.uint8)]),
               (self.image,[np.zeros((16,16),np.uint8)]),(self.image,[self.mask,self.mask])]
        with mock.patch.object(hybrid,'native_baseline',side_effect=AssertionError('invalid data reached planner')):
            for image,masks in cases:
                for diagnostic in (False,True):
                    with self.subTest(shape=getattr(image,'shape',None),diagnostic=diagnostic),self.assertRaises(ValueError):
                        hybrid.plan(image,masks,diagnostic)

    def test_soft_alpha_cannot_be_enabled_by_diagnostic_mode(self):
        soft=self.image.copy();soft[8,8,3]=254
        for diagnostic in (False,True):
            with self.assertRaisesRegex(ValueError,'nonopaque'):
                hybrid.plan(soft,[self.mask],diagnostic)

    def test_noncontiguous_programmatic_input_is_normalized(self):
        image=self.image[:,::-1,:];mask=self.mask[:,::-1]
        a,masks=hybrid.validated_inputs(image,[mask]);self.assertTrue(a.flags.c_contiguous)
        self.assertTrue(masks[0].flags.c_contiguous);self.assertTrue(np.array_equal(a,image))
        first,base_a,final_a=hybrid.plan(image,[mask]);second,base_b,final_b=hybrid.plan(image.copy(),[mask.copy()])
        self.assertEqual(first['strokes'],second['strokes']);self.assertTrue(np.array_equal(final_a,final_b))
        self.assertTrue(np.array_equal(base_a,base_b));self.assertEqual(first['addedEvents'],0)

    def test_cli_rejects_size_dimensions_optin_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);rgba=root/'source.rgba';mask=root/'mask.u8'
            self.image.tofile(rgba);self.mask.tofile(mask)
            original=rgba.read_bytes()
            command=[sys.executable,str(Path(hybrid.__file__)),'--research','--rgba',str(rgba),'--width','16','--height','16','--mask',str(mask),'--output',str(root/'plan')]
            missing=command.copy();missing.remove('--research')
            self.assertNotEqual(subprocess.run(missing,capture_output=True).returncode,0)
            dimensions=command.copy();dimensions[dimensions.index('--width')+1]='0'
            self.assertNotEqual(subprocess.run(dimensions,capture_output=True).returncode,0)
            rgba.write_bytes(original+b'extra')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            self.assertFalse((root/'plan').exists());rgba.write_bytes(original)
            (root/'plan').mkdir();sentinel=root/'plan/plan.json';sentinel.write_text('previous plan')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            self.assertEqual(sentinel.read_text(),'previous plan');self.assertEqual(rgba.read_bytes(),original)

    def test_correction_budget_and_minimum_geometry_are_enforced(self):
        result,_,_=hybrid.plan(self.image,[self.mask])
        self.assertEqual(result['budget'],dict(maxEvents=2048,maxSamples=250000,maxCorrections=192,maxShortCorrections=0))
        proposed=dict(color=[.3,.4,.5,1.],width=1.,geometry=[(2.5,2.5,1.),(6.5,2.5,1.)])
        region=dict(object=0,mask=self.mask>0,color=np.ones(4),area=256,value=3.,isolatedExtremum=False)
        def accepted(canvas,target,stroke,mask,min_gain):
            stroke.update(opacity=1.,gain=1.);return True
        with mock.patch.object(hybrid,'correction_regions',return_value=[region]),mock.patch.object(hybrid,'correction_paths',side_effect=lambda *a:[dict(proposed,geometry=list(proposed['geometry'])) for _ in range(300)]),mock.patch.object(hybrid,'fit',side_effect=accepted):
            result,_,_=hybrid.plan(self.image,[self.mask])
        self.assertEqual(result['addedEvents'],192)
        self.assertLessEqual(len(result['strokes']),2048);self.assertLessEqual(result['samples'],250000)
        self.assertTrue(all(hybrid.length(s)>=3 for s in result['strokes'][result['baselineEvents']:]))

    def test_repo_sibling_import_layout_does_not_require_research_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'tools/experimental'
            for name,source,files in [('hybrid_planner',Path(hybrid.__file__).parent,['hybrid.py']),
                                      ('flow_planner',hybrid.FLOW,['flow.py','geometry.py','ridge.py']),
                                      ('automatic_stages',hybrid.LEGACY,['plan.py','contract.py'])]:
                dest=root/name;dest.mkdir(parents=True)
                for file in files:shutil.copy2(source/file,dest/file)
            script="import hybrid; assert hybrid.FLOW.name=='flow_planner'; assert hybrid.LEGACY.name=='automatic_stages'"
            run=subprocess.run([sys.executable,'-c',script],cwd=root/'hybrid_planner',capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_batch_refuses_empty_manifest_and_unknown_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);manifest=root/'manifest.json'
            command=[sys.executable,str(Path(__file__).with_name('run_evaluation.py')),'--manifest',str(manifest),'--output',str(root/'out')]
            for content in ({},dict(fixtures=[]),dict(fixtures=[None])):
                manifest.write_text(json.dumps(content));run=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(run.returncode,2);self.assertIn('invalid manifest',run.stderr);self.assertFalse((root/'out').exists())
            manifest.write_text(json.dumps(dict(fixtures=[dict(id='test',width=16,height=16,rgba='input',masks=['mask'])])))
            run=subprocess.run(command+['--id','missing'],capture_output=True,text=True)
            self.assertEqual(run.returncode,2);self.assertFalse((root/'out').exists())

    def test_native_roundtrip_matches_feedback_and_refuses_overwrite(self):
        self.assertTrue(self.runner.is_file(),'build the native hybrid runner, including any explicitly configured path')
        image=self.image.copy();image[4:12,4:12,:3]=(210,100,70)
        result,baseline,final=hybrid.plan(image,[self.mask])
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);image.tofile(root/'source.rgba');self.mask.tofile(root/'mask.u8')
            hybrid.write_plan(result,root/'plan')
            command=[str(self.runner),str(root/'plan/plan.txt'),str(root/'source.rgba'),str(root/'native'),str(root/'mask.u8')]
            run=subprocess.run(command,capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
            rendered=(root/'native/final.rgba').read_bytes();self.assertEqual(rendered,final.tobytes())
            audit=json.loads((root/'native/audit.json').read_text());self.assertEqual(audit['sourceAssetReads'],0)
            self.assertEqual(set(audit['resourceKinds']),{'Mask'});self.assertTrue(audit['savedReopenedVerified'])
            self.assertTrue(all(b==0 for b in (root/'native/zero.rgba').read_bytes()))
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            self.assertEqual((root/'native/final.rgba').read_bytes(),rendered)

    def test_native_enforces_opaque_alpha_and_declared_correction_boundary(self):
        self.assertTrue(self.runner.is_file(),'build the native hybrid runner')
        base='0 0 1 1 1 .2 .4 .6 1 2\n2.5 2.5 1\n6.5 2.5 1\n'
        valid='0 2 1 1 1 .2 .4 .6 1 2\n2.5 8.5 1\n6.5 8.5 1\n'
        cases=[('DBHYBRID1 16 16 1 1 1\n'+base,a) for a in (0,128,254)]
        cases += [('DBHYBRID1 16 16 1 194 1\n',255),
                  ('DBHYBRID1 16 16 1 2 1\n'+base+'0 2 1 1 1 .2 .4 .6 1 1\n2.5 8.5 1\n',255),
                  ('DBHYBRID1 16 16 1 2 1\n'+base+'0 2 1 1 1 .2 .4 .6 1 2\n2.5 8.5 1\n4.5 8.5 1\n',255),
                  ('DBHYBRID1 16 16 1 2 1\n'+base+'0 2 1 1 1 .2 .4 .6 1 3\n2.5 8.5 1\n2.5 8.5 1\n6.5 8.5 1\n',255),
                  ('DBHYBRID1 16 16 1 2 1\n'+base+valid.replace('0 2 ','0 1 ',1),255)]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.mask.tofile(root/'mask.u8')
            for i,(plan,alpha) in enumerate(cases):
                image=self.image.copy();image[:,:,3]=alpha;image.tofile(root/'source.rgba');(root/'plan.txt').write_text(plan)
                output=root/f'out-{i}'
                run=subprocess.run([str(self.runner),str(root/'plan.txt'),str(root/'source.rgba'),str(output),str(root/'mask.u8')],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0,run.stdout);self.assertFalse(output.exists())
            self.image.tofile(root/'source.rgba');(root/'plan.txt').write_text('DBHYBRID1 16 16 1 2 1\n'+base+valid)
            run=subprocess.run([str(self.runner),str(root/'plan.txt'),str(root/'source.rgba'),str(root/'valid'),str(root/'mask.u8')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            audit=json.loads((root/'valid/audit.json').read_text())
            self.assertEqual(audit['declaredCorrectionEvents'],1);self.assertEqual(audit['baselineAuthenticity'],'not-certified-by-importer')

    def test_native_rejects_event_sample_mask_and_segment_budget(self):
        self.assertTrue(self.runner.is_file(),'build native hybrid before testing')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.image.tofile(root/'source.rgba');self.mask.tofile(root/'mask.u8')
            cases=['DBHYBRID1 16 16 1 2049 2049\n',
                   'DBHYBRID1 16 16 1 1 1\n0 0 1 1 1 .2 .4 .6 1 250001\n',
                   'DBHYBRID1 16 16 1 1 1\n0 0 1 1 1 .2 .4 .6 1 2\n2.5 8.5 1\n13.5 8.5 1\n']
            for i,plan in enumerate(cases):
                (root/'plan.txt').write_text(plan)
                mask=self.mask.copy()
                if i==2:mask[:,8]=0
                mask.tofile(root/'mask.u8');output=root/f'out-{i}'
                run=subprocess.run([str(self.runner),str(root/'plan.txt'),str(root/'source.rgba'),str(output),str(root/'mask.u8')],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0,run.stdout);self.assertFalse(output.exists())
            (root/'mask.u8').write_bytes(b'bad')
            run=subprocess.run([str(self.runner),str(root/'plan.txt'),str(root/'source.rgba'),str(root/'bad-mask'),str(root/'mask.u8')],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0);self.assertFalse((root/'bad-mask').exists())


if __name__=='__main__':unittest.main()
