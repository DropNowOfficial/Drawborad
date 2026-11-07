import copy
import time
import unittest
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from unittest import mock
import numpy as np
import locality as l
import cli


def event(i, xy, end=None, bounds=None, block=0, stage='base', layer='L', obj='O'):
    if end is None:
        end = xy
    if bounds is None:
        bounds = [int(xy[0]),int(xy[1]),int(xy[0])+1,int(xy[1])+1]
    return dict(ordinal=i,first=list(xy),last=list(end),bounds=bounds,block=block,
                stage=stage,layerId=layer,objectId=obj,sampleCount=2)


def metadata(events):
    return dict(schemaVersion=1,width=128,height=128,eventCount=len(events),
                sampleCount=2*len(events),gapUs=80000,events=events)


class LocalityTests(unittest.TestCase):
    def test_disjoint_nearest_ready_reduces_jump_without_input_mutation(self):
        m=metadata([event(0,(1,1)),event(1,(110,10)),event(2,(5,5)),event(3,(9,9))])
        old=copy.deepcopy(m)
        permutation, stats=l.schedule(m,1)
        self.assertEqual(permutation,[0,2,3,1])
        self.assertLess(stats['retainedTravel']['total'],stats['originalTravel']['total'])
        self.assertEqual(m,old)

    def test_overlap_chain_cannot_invert(self):
        events=[event(0,(1,1),bounds=[0,0,20,20]),
                event(1,(21,1),bounds=[10,0,30,20]),
                event(2,(41,1),bounds=[25,0,45,20])]
        self.assertEqual(l.schedule(metadata(events),0)[0],[0,1,2])
        with self.assertRaisesRegex(ValueError,'dependency'):
            l.validate_permutation(events,[0,2,1],0)

    def test_conservative_same_box_and_zero_width_box(self):
        events=[event(0,(1,1),bounds=[0,0,100,100]),
                event(1,(90,90),bounds=[0,0,100,100]),
                event(2,(3,3),bounds=[0,0,100,100])]
        p,s=l.schedule(metadata(events),0)
        self.assertEqual(p,[0,1,2])
        self.assertEqual(s['blocks'][0]['conflicts'],3)
        self.assertFalse(l.overlapping(np.array([3,3,3,5]),np.array([[0,0,8,8]]))[0])

    def test_half_open_bbox_edges_not_centerline(self):
        a=np.array([0,0,20,20])
        self.assertEqual(l.overlapping(a,np.array([[19,0,30,20],[20,0,30,20]])).tolist(),[True,False])

    def test_primer_layer_object_boundaries_are_not_crossed(self):
        events=[event(0,(1,1)),event(1,(90,90)),
                event(2,(2,2),block=1,stage='form',layer='F'),
                event(3,(3,3),block=2,stage='form',layer='F2',obj='O2')]
        with self.assertRaisesRegex(ValueError,'primer'):
            l.validate_permutation(events,[1,0,2,3],1)
        with self.assertRaisesRegex(ValueError,'cross-block'):
            l.validate_permutation(events,[0,2,1,3],1)
        self.assertEqual(l.schedule(metadata(events),1)[0],[0,1,2,3])

    def test_equal_distance_ties_stable_and_repeated_coordinates_valid(self):
        events=[event(0,(5,5)),event(1,(7,5)),event(2,(5,7))]
        p,_=l.schedule(metadata(events),1)
        self.assertEqual(p,[0,1,2])
        self.assertEqual(l.schedule(metadata(events),1)[0],p)

    def test_all_transitions_include_block_boundaries(self):
        events=[event(0,(0,0)),event(1,(30,40),block=1,stage='form',layer='F'),
                event(2,(30,50),block=2,stage='detail',layer='D')]
        travel=l.travel(events,[0,1,2])
        self.assertEqual(travel['total'],60)
        self.assertEqual([p['total'] for p in travel['phases']],[0,50,10])

    def test_budget_deadline_invalid_metadata_and_bijection(self):
        m=metadata([event(0,(1,1)),event(1,(3,3))])
        with self.assertRaisesRegex(ValueError,'storage'):
            l.schedule(m,0,graph_bytes=0)
        with self.assertRaises(TimeoutError):
            l.schedule(m,0,deadline=time.perf_counter()-1)
        with self.assertRaisesRegex(ValueError,'bijection'):
            l.validate_permutation(m['events'],[0,0],0)
        m['events'][0]['bounds']=[0,0,129,10]
        with self.assertRaisesRegex(ValueError,'bbox'):
            l.schedule(m,0)

    def test_greedy_worse_total_retains_original(self):
        points=[[59,15],[7,35],[49,59],[36,101],[68,115]]
        events=[event(i,p) for i,p in enumerate(points)]
        p,stats=l.schedule(metadata(events),1)
        self.assertEqual(stats['rawCandidatePermutation'],[0,2,3,4,1])
        self.assertFalse(stats['rawCandidateRetained'])
        self.assertGreater(stats['proposedTravel']['total'],stats['originalTravel']['total'])
        self.assertEqual(p,list(range(5)))


class ContractTests(unittest.TestCase):
    def test_declaration_is_hash_bound_canonical_and_nonempty_prefix(self):
        good=f"DBLOCALITY1 {'a'*64} {'b'*64} 5 1\n".encode()
        self.assertEqual(cli.declaration(good)['immutablePrefixCount'],1)
        for bad in (good+b'extra\n', good.replace(b'5 1',b'5 0'),
                    good.replace(b'5 1',b'5 6'),good.replace(b'5 1',b'05 1'),
                    good.replace(b'a',b'A'),b'x'*257):
            with self.subTest(bad=bad),self.assertRaises(ValueError):cli.declaration(bad)

    def test_explicit_missing_runner_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch.dict(os.environ,{'DRAWBORAD_NATIVE_LOCALITY':str(Path(temp)/'missing')}):
                with self.assertRaisesRegex(ValueError,'runner missing'):cli.native_path()

    def test_symlink_and_sidecars_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'source.draw';source.write_bytes(b'test')
            alias=root/'alias';alias.symlink_to(source)
            with self.assertRaisesRegex(ValueError,'symlink'):cli.read_bounded(alias,100)
            missing=root/'dangling';missing.symlink_to(root/'absent')
            with self.assertRaisesRegex(ValueError,'symlink'):cli.new_path(missing)
            Path(str(source)+'.journal').symlink_to(root/'absent')
            with self.assertRaisesRegex(ValueError,'sidecar'):cli.input_contract(source)

    def test_failure_after_output_creation_is_recorded_not_completed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'source.draw';source.write_bytes(b'fixture')
            declaration=root/'prefix.txt'
            declaration.write_text(f"DBLOCALITY1 {hashlib.sha256(source.read_bytes()).hexdigest()} {'0'*64} 1 1\n")
            with mock.patch.object(cli,'run_native',side_effect=RuntimeError('deliberate native failure')):
                with self.assertRaisesRegex(RuntimeError,'deliberate'):
                    cli.reorder(source,declaration,root/'output',root/'unused')
            result=json.loads((root/'output/result.json').read_text())
            self.assertEqual(result['status'],'failed');self.assertFalse(result['integrityPassed'])
            with self.assertRaisesRegex(ValueError,'exists'):
                cli.reorder(source,declaration,root/'output',root/'unused')


class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        explicit=os.environ.get('DRAWBORAD_NATIVE_LOCALITY')
        default=Path(__file__).resolve().parent/'build/native-locality'
        if not explicit and not default.is_file():
            raise unittest.SkipTest('local default runner absent; CI must set DRAWBORAD_NATIVE_LOCALITY')
        # An explicit missing path is an error, never a skip or false green.
        cls.binary=cli.native_path()
        cls.temporary=tempfile.TemporaryDirectory()
        cls.root=Path(cls.temporary.name)
        cls.cases=cls.root/'cases'
        run=subprocess.run([str(cls.binary),'--research','self-test',str(cls.cases)],capture_output=True,text=True,timeout=60)
        if run.returncode:raise RuntimeError(run.stderr)
        cls.source=cls.cases/'scheduler-input.draw'
        cls.declaration=cls.root/'prefix.txt'
        cls.call('declare',cls.source,1,cls.declaration)

    @classmethod
    def tearDownClass(cls):cls.temporary.cleanup()

    @classmethod
    def call(cls,*args,ok=True):
        run=subprocess.run([str(cls.binary),'--research',*map(str,args)],capture_output=True,text=True,timeout=60)
        if ok and run.returncode:raise AssertionError(run.stderr)
        if not ok and run.returncode==0:raise AssertionError('native unexpectedly succeeded')
        return run

    def test_native_synthetic_checks_are_real_and_nonempty(self):
        result=json.loads((self.cases/'self-tests.json').read_text())
        self.assertTrue(result['passed']);self.assertGreaterEqual(len(result['tests']),44)
        self.assertEqual(result['sourceAssetReads'],0)

    def test_native_requires_research_before_output(self):
        output=self.root/'without-flag'
        run=subprocess.run([str(self.binary),'inspect',str(self.source),str(output)],capture_output=True)
        self.assertNotEqual(run.returncode,0);self.assertFalse(output.exists())

    def test_native_wrong_declaration_rejected_before_output(self):
        original=self.declaration.read_bytes()
        for index,bad in enumerate((original.replace(b'DBLOCALITY1',b'DBFLOW1'),
                                    original.replace(b' 5 1\n',b' 5 0\n'),
                                    original.replace(b' 5 1\n',b' 5 6\n'),
                                    original+b'extra\n')):
            decl=self.root/f'bad-declaration-{index}.txt';decl.write_bytes(bad)
            output=self.root/f'rejected-declaration-{index}'
            self.call('reorder',self.source,self.cases/'scheduler-known-valid.txt',decl,output,ok=False)
            self.assertFalse(output.exists())

    def test_native_wrong_source_hash_and_state_rejected(self):
        original=self.declaration.read_text().split()
        for position in (1,2):
            wrong=original.copy();wrong[position]='0'*64
            decl=self.root/f'wrong-binding-{position}';decl.write_text(' '.join(wrong)+'\n')
            output=self.root/f'rejected-binding-{position}'
            self.call('reorder',self.source,self.cases/'scheduler-known-valid.txt',decl,output,ok=False)
            self.assertFalse(output.exists())

    def test_native_sidecar_symlink_and_prefix_inversion_rejected(self):
        alias=self.root/'project-link.draw';alias.symlink_to(self.source)
        out=self.root/'rejected-symlink'
        self.call('inspect',alias,out,ok=False);self.assertFalse(out.exists())
        copy=self.root/'sidecar.draw';copy.write_bytes(self.source.read_bytes())
        Path(str(copy)+'.bak').symlink_to(self.root/'not-present')
        out=self.root/'rejected-sidecar';self.call('inspect',copy,out,ok=False);self.assertFalse(out.exists())
        bad=self.root/'prefix-inversion.txt';bad.write_text('1\n0\n2\n3\n4\n')
        out=self.root/'rejected-prefix';self.call('reorder',self.source,bad,self.declaration,out,ok=False)
        self.assertFalse(out.exists())

    def test_cli_end_to_end_new_project_is_exact_and_no_clobber(self):
        before=self.source.read_bytes();output=self.root/'cli-result'
        result=cli.reorder(self.source,self.declaration,output,self.binary)
        self.assertEqual(result['status'],'completed');self.assertTrue(result['integrityPassed'])
        self.assertLessEqual(result['retainedTravel']['total'],result['originalTravel']['total'])
        self.assertEqual(before,self.source.read_bytes())
        native=json.loads((output/'native/checks.json').read_text())
        self.assertTrue(native['originalFinalLayerPhasePixelsEqual'])
        self.assertTrue(native['relativePointTimesEqual']);self.assertEqual(native['immutablePrefixCount'],1)
        with self.assertRaisesRegex(ValueError,'exists'):
            cli.reorder(self.source,self.declaration,output,self.binary)

    def test_cli_from_unrelated_cwd_and_input_alias_rejected(self):
        script=Path(__file__).resolve().parent/'cli.py'
        output=self.root/'unrelated-cwd'
        run=subprocess.run([sys.executable,str(script),'--research','--native',str(self.binary),
                            'reorder','--input',str(self.source),'--declaration',str(self.declaration),
                            '--output',str(output)],cwd='/tmp',capture_output=True,text=True,timeout=60)
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertEqual(json.loads((output/'result.json').read_text())['status'],'completed')
        run=subprocess.run([sys.executable,str(script),'--research','--native',str(self.binary),
                            'reorder','--input',str(self.source),'--declaration',str(self.declaration),
                            '--output',str(self.source)],capture_output=True,text=True)
        self.assertNotEqual(run.returncode,0)


if __name__=='__main__':unittest.main()
