import unittest,io
from PIL import Image
from cloud.experimental_masks import preview_masked_crop as apply

def png(im):
    out=io.BytesIO();im.save(out,format='PNG');return out.getvalue()
def read(b):return Image.open(io.BytesIO(b)).convert('RGBA')
class MaskTests(unittest.TestCase):
    def test_toggle_retains_bytes_and_alpha(self):
        s=png(Image.new('RGBA',(3,1),(30,60,90,128)));m=Image.new('L',(3,1));m.putdata([0,128,255]);m=png(m)
        self.assertEqual(apply(s,m,(0,0),(0,0),(3,1),False),s)
        p=list(read(apply(s,m,(0,0),(0,0),(3,1))).getdata());self.assertEqual([x[3]for x in p],[0,64,128]);self.assertTrue(all(x[:3]==(30,60,90)for x in p))
        self.assertEqual(list(read(s).getdata())[0][3],128)
    def test_roi_alignment_and_outside(self):
        s=png(Image.new('RGBA',(3,1),'red'));m=png(Image.new('L',(1,1),255))
        self.assertEqual([x[3]for x in read(apply(s,m,(2,0),(3,0),(5,1))).getdata()],[0,255,0])
    def test_refuse_ambiguous_mask_bounds_and_boolean(self):
        s=png(Image.new('RGBA',(2,2),'red'));m=png(Image.new('L',(2,2),255))
        for args in [(s,s,(0,0),(0,0),(2,2),True),(s,m,(1,0),(0,0),(2,2),True),(s,m,(0,0),(0,0),(2,2),1)]:
            with self.assertRaises(ValueError):apply(*args)
if __name__=='__main__':unittest.main()
