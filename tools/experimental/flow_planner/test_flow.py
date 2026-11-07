import math
import ctypes as C
import json
import subprocess
import sys
import time
from pathlib import Path
import tempfile
import unittest
import numpy as np
import flow

class RidgeTests(unittest.TestCase):
    def curved_line(self):
        y,x=np.mgrid[:64,:64];center=31+2*np.sin(x/12)
        luminance=.15+.7*np.exp(-.5*((y-center)/.7)**2)
        return np.dstack([luminance]*3+[np.ones_like(luminance)]),center
    def test_ridge_trace_follows_line_center_not_parallel_edges(self):
        from ridge import ridge_field,trace_ridge
        source,center=self.curved_line();field=ridge_field(source)
        path=trace_ridge((20.5,float(center[0,20]+.5)),field,np.full((64,64),255,np.uint8),36)
        self.assertIsNotNone(path)
        length=sum(math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(path,path[1:]))
        self.assertGreater(length,20)
        self.assertLess(max(abs(y-.5-(31+2*math.sin((x-.5)/12))) for x,y,p in path),.65)
    def test_ridge_trace_respects_existing_mask_hole(self):
        from ridge import ridge_field,trace_ridge
        source,_=self.curved_line();mask=np.full((64,64),255,np.uint8);mask[:,30:33]=0
        path=trace_ridge((20.5,33),ridge_field(source),mask,60)
        self.assertIsNotNone(path)
        for a,b in zip(path,path[1:]):
            for t in np.linspace(0,1,20):
                self.assertNotEqual(mask[int(a[1]+t*(b[1]-a[1])),int(a[0]+t*(b[0]-a[0]))],0)
        self.assertTrue(all(x<30 for x,y,p in path))
    def test_flat_field_has_no_invented_ridges(self):
        from ridge import ridge_field
        field=ridge_field(np.full((32,32,4),.5))
        self.assertFalse(field['eligible'].any())

class NativeFeedbackTests(unittest.TestCase):
    def test_alpha_constraint_rejects_overpainting_not_masks_reference(self):
        target=np.full((20,20,4),255,np.uint8);target[:,:,3]=64
        with_canvas=flow.NativeCanvas(target)
        stroke=dict(width=4.,hardness=1.,geometry=[(3.5,10.5,1.),(16.5,10.5,1.)])
        box,cov=with_canvas.candidate(stroke,np.full((20,20),255,np.uint8))
        self.assertLess(with_canvas.evaluate([1,1,1,1],1.),-1e20)
        limit=flow.alpha_limit(target[box],with_canvas.pixels[box],cov)
        self.assertLessEqual(limit,65/255)
        self.assertGreater(with_canvas.evaluate([1,1,1,1],limit),0)
        with_canvas.commit()
        self.assertLessEqual(with_canvas.pixels[:,:,3].max(),65)
        self.assertGreater(with_canvas.pixels[:,:,3].max(),0)
    def test_constant_source_does_not_invent_empty_phases(self):
        image=np.empty((24,24,4),np.uint8);image[:]=(180,120,70,255)
        result,pixels=flow.plan(image,[np.full((24,24),255,np.uint8)])
        self.assertEqual({s['phase'] for s in result['strokes']},{'base'})
        self.assertTrue(np.array_equal(pixels,image))
    def test_ffi_rejects_malformed_arrays_and_wrong_call_order(self):
        for bad in [np.zeros((8,8,3),np.uint8),np.zeros((8,8,4),np.float32),np.zeros((0,8,4),np.uint8)]:
            with self.subTest(shape=bad.shape),self.assertRaises(ValueError):flow.NativeCanvas(bad)
        image=np.full((16,16,4),255,np.uint8);native=flow.NativeCanvas(image);mask=np.full((16,16),255,np.uint8)
        stroke=dict(width=2.,hardness=.5,geometry=[(3.5,8.5,1.),(12.5,8.5,1.)])
        with self.assertRaises(ValueError):native.commit()
        with self.assertRaises(ValueError):native.evaluate([.5,.5,.5,1.],1.)
        for invalid_mask in [mask[:,:8],mask.astype(float),np.full((16,16),128,np.uint8)]:
            with self.assertRaises(ValueError):native.candidate(stroke,invalid_mask)
        native.candidate(stroke,mask)
        for invalid_color in [[.5,.5,.5],[.5,.5,.5,1.,0.],[float('nan'),.5,.5,1.]]:
            with self.assertRaises(ValueError):native.evaluate(invalid_color,1.)
            with self.assertRaises(ValueError):native.commit()
        native.candidate(stroke,mask);native.evaluate([.5,.5,.5,1.],1.);native.commit()
        with self.assertRaises(ValueError):native.commit()
        native.candidate(stroke,mask);native.evaluate([.5,.5,.5,1.],1.)
        with self.assertRaises(ValueError):native.candidate(dict(stroke,geometry=[(30.,8.,1.)]),mask)
        with self.assertRaises(ValueError):native.commit()
        native.close()
        with self.assertRaises(ValueError):native.phase()

    def test_native_abi_rejects_stale_pending_and_short_color_buffer(self):
        image=np.full((16,16,4),255,np.uint8);native=flow.NativeCanvas(image);mask=np.full((16,16),255,np.uint8)
        stroke=dict(width=2.,hardness=.5,geometry=[(3.5,8.5,1.),(12.5,8.5,1.)])
        native.candidate(stroke,mask)
        self.assertEqual(native.lib.flow_commit(native.handle),-1)
        native.evaluate([.5,.5,.5,1.],1.)
        short=(C.c_double*3)(.5,.5,.5)
        self.assertTrue(math.isnan(native.lib.flow_evaluate(native.handle,short,3,1.)))
        self.assertEqual(native.lib.flow_commit(native.handle),-1)
        native.candidate(stroke,mask);native.evaluate([.5,.5,.5,1.],1.)
        points=(C.c_double*3)(1.5,1.5,1.);bounds=(C.c_int*4)()
        self.assertNotEqual(native.lib.flow_candidate(native.handle,2.,.5,2,points,3,mask.ctypes.data_as(C.POINTER(C.c_uint8)),mask.size,bounds),0)
        self.assertEqual(native.lib.flow_commit(native.handle),-1)
        native.phase();self.assertTrue(np.all(native.pixels==0))

    def test_batch_rejects_empty_invalid_and_missing_fixture_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);manifest=root/'manifest.json'
            cases=[None,{},dict(fixtures=[]),dict(fixtures=[None]),dict(fixtures=[dict(id='../bad',width=24,height=24,rgba='a',masks=['b'])])]
            for index,content in enumerate(cases):
                manifest.write_text(json.dumps(content));output=root/f'out-{index}'
                run=subprocess.run([sys.executable,str(Path(__file__).with_name('run_evaluation.py')),'--manifest',str(manifest),'--output',str(output)],capture_output=True,text=True)
                self.assertEqual(run.returncode,2);self.assertIn('invalid manifest',run.stderr);self.assertFalse(output.exists())
            manifest.write_text(json.dumps(dict(fixtures=[dict(id='okay',width=24,height=24,rgba='a',masks=['b'])])))
            run=subprocess.run([sys.executable,str(Path(__file__).with_name('run_evaluation.py')),'--manifest',str(manifest),'--output',str(root/'unknown'),'--id','missing'],capture_output=True,text=True)
            self.assertEqual(run.returncode,2);self.assertFalse((root/'unknown').exists())

    def test_ffi_noncontiguous_views_match_contiguous_arrays(self):
        full=np.empty((18,20,4),np.uint8);y,x=np.mgrid[:18,:20];full[:,:,0]=x*9;full[:,:,1]=y*7;full[:,:,2]=35;full[:,:,3]=255;source=full[:,::-1,:]
        self.assertFalse(source.flags.c_contiguous)
        mask=np.full((18,20),255,np.uint8);mask[:,:2]=0;mask=mask[:,::-1]
        colors=np.array([.2,0,.4,0,.6,0,1.,0])[::2]
        geometry=np.array([[2.5,0,4.5,0,.8,0],[16.5,0,12.5,0,1.,0]])[:,::2]
        a,b=flow.NativeCanvas(source),flow.NativeCanvas(np.ascontiguousarray(source));gains=[]
        for context,reviewed,points,color in [(a,mask,geometry,colors),(b,mask.copy(),geometry.copy(),colors.copy())]:
            context.candidate(dict(width=4.,hardness=.5,geometry=points),reviewed)
            gains.append(context.evaluate(color,.7));context.commit()
        self.assertEqual(gains[0],gains[1])
        self.assertTrue(np.array_equal(a.pixels,b.pixels))
        retained=a.pixels;a.close();self.assertTrue(np.array_equal(retained,b.pixels))

    def test_gradient_reward_uses_native_spatial_error(self):
        target=np.full((16,16,4),128,np.uint8);target[:,:,3]=255;target[8,2:14,:3]=240
        native=flow.NativeCanvas(target);mask=np.full((16,16),255,np.uint8)
        native.candidate(dict(width=64.,hardness=1.,geometry=[(1.5,1.5,1),(14.5,14.5,1)]),mask)
        native.evaluate([128/255]*3+[1],1);native.commit()
        baseline=native.pixels.copy()
        native.candidate(dict(width=1.,hardness=1.,geometry=[(2.5,8.5,1),(13.5,8.5,1)]),mask)
        color=[240/255]*3+[1]
        ordinary=native.evaluate(color,1);native.gradients(.85);with_gradient=native.evaluate(color,1)
        self.assertGreater(with_gradient,ordinary)
        self.assertTrue(np.array_equal(native.pixels,baseline))
        native.commit();self.assertGreater(native.pixels[8,6,0],baseline[8,6,0])

    def test_native_phase_replay_equals_in_memory_feedback(self):
        root=Path(__file__).resolve().parent;runner=root/'build/native-flow'
        self.assertTrue(runner.is_file(),'build the actual native research runner before testing')
        target=np.full((24,24,4),255,np.uint8);left=np.zeros((24,24),np.uint8);left[:,:12]=255;masks=[left,255-left]
        native=flow.NativeCanvas(target);strokes=[]
        for phase in flow.PHASES:
            if phase!='base':native.phase()
            for oi,x in enumerate([6.,18.]):
                stroke=dict(object=oi,phase=phase,width=8.,hardness=.2,opacity=.55,color=[.3,.55,.8,1.],geometry=[(x,4.5,.7),(x+1,12.5,1.),(x,20.5,.8)])
                native.candidate(stroke,masks[oi]);native.evaluate(stroke['color'],stroke['opacity']);native.commit();strokes.append(stroke)
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);target.tofile(out/'source.rgba')
            for i,mask in enumerate(masks):mask.tofile(out/f'mask-{i}.u8')
            flow.write_plan(dict(width=24,height=24,objects=2,strokes=strokes),out/'plan')
            command=[str(runner),str(out/'plan/plan.txt'),str(out/'source.rgba'),str(out/'native'),str(out/'mask-0.u8'),str(out/'mask-1.u8')]
            run=subprocess.run(command,capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
            actual=np.fromfile(out/'native/final.rgba',np.uint8).reshape(24,24,4)
            self.assertTrue(np.array_equal(actual,native.pixels))
            audit=json.loads((out/'native/audit.json').read_text());self.assertEqual(audit['sourceAssetReads'],0)
            self.assertEqual(set(audit['resourceKinds']),{'Mask'});self.assertEqual(len(audit['layers']),6)
            self.assertEqual(subprocess.run(command,capture_output=True).returncode,1)

    def test_native_directory_created_during_planning_is_not_clobbered(self):
        runner=Path(__file__).with_name('build')/'native-flow'
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);np.full((24,24,4),255,np.uint8).tofile(root/'source.rgba')
            np.full((24,24),255,np.uint8).tofile(root/'mask.u8')
            # A large but valid input leaves a real parse/compile window after
            # the initial exists() check. No production-only test hook is used.
            (root/'plan.txt').write_text('DBFLOW1 24 24 1 1\n0 0 1 1 1 .3 .5 .7 1 100000\n'+'8.5 8.5 1\n'*100000)
            args=[str(runner),str(root/'plan.txt'),str(root/'source.rgba'),str(root/'native'),str(root/'mask.u8')]
            process=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            time.sleep(.005)
            (root/'native').mkdir()
            sentinel=root/'native/final.rgba';sentinel.write_bytes(b'existing owner data')
            stdout,stderr=process.communicate(timeout=30)
            self.assertEqual(process.returncode,1,stdout+stderr)
            self.assertEqual(sentinel.read_bytes(),b'existing owner data')
            self.assertFalse((root/'native/automatic.draw').exists())

    def test_soft_alpha_rejected_without_output_or_input_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);rgba=np.full((24,24,4),128,np.uint8);mask=np.full((24,24),255,np.uint8)
            rgba.tofile(out/'source.rgba');mask.tofile(out/'mask.u8')
            original=(out/'source.rgba').read_bytes()
            run=subprocess.run([sys.executable,str(Path(__file__).with_name('flow.py')),'--research','--rgba',str(out/'source.rgba'),'--width','24','--height','24','--mask',str(out/'mask.u8'),'--output',str(out/'plan')],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0);self.assertIn('unsupported soft alpha',run.stdout+run.stderr)
            self.assertFalse((out/'plan').exists());self.assertEqual((out/'source.rgba').read_bytes(),original)

    def test_gradient_is_local_color_and_repeatable_not_global_palette(self):
        y,x=np.mgrid[:24,:32];image=np.empty((24,32,4),np.uint8)
        image[:,:,0]=40+x*5;image[:,:,1]=60+y*5;image[:,:,2]=210-x*3;image[:,:,3]=255
        mask=np.full((24,32),255,np.uint8)
        first,a=flow.plan(image,[mask]);second,b=flow.plan(image,[mask])
        self.assertEqual(first['strokes'],second['strokes']);self.assertTrue(np.array_equal(a,b))
        self.assertLess(np.mean(np.abs(a[:,:,:3].astype(float)-image[:,:,:3]))/255,.025)
        self.assertGreater(len({tuple(s['color']) for s in first['strokes']}),12)

    def test_source_and_mask_budgets_remain_enforced(self):
        image=np.full((24,24,4),255,np.uint8);mask=np.full((24,24),255,np.uint8)
        with self.assertRaisesRegex(ValueError,'overlapping'):flow.plan(image,[mask,mask])
        bad=mask.copy();bad[0,0]=12
        with self.assertRaisesRegex(ValueError,'binary'):flow.plan(image,[bad])
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'input';path.write_bytes(b'12345')
            with self.assertRaisesRegex(ValueError,'byte size'):flow.exact(path,4)
            self.assertEqual(path.read_bytes(),b'12345')

if __name__=='__main__':unittest.main()
