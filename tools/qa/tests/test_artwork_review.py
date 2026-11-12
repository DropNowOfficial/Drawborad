import json
import numpy as np
import pytest
from PIL import Image,ImageFilter
from tools.qa.artwork_review import compare,write_review

def test_identical_pixels_do_not_certify_art_or_native_editability():
    a=Image.new('RGBA',(64,64),'white');r=compare(a,a,[{'name':'face','box':[0,0,32,32]}])
    assert r['regions'][0]['max_error_u8']==0
    assert r['artistic_acceptance']=='not_reviewed' and r['native_editability']=='not_verified'

def test_small_critical_damage_is_not_hidden_by_global_average():
    a=Image.new('RGB',(100,100),'white');b=a.copy();b.paste('black',(5,5,10,10))
    r=compare(a,b,[{'name':'face','box':[5,5,10,10]}])
    assert r['whole']['mean_visible_error_u8']<1
    assert r['regions'][0]['mean_visible_error_u8']==255
    assert r['regions'][0]['darkening_mean_u8']==255

def test_blur_has_separate_edge_measurements():
    x=np.zeros((64,64,3),dtype=np.uint8);x[:,::4]=255
    a=Image.fromarray(x);b=a.filter(ImageFilter.GaussianBlur(1))
    r=compare(a,b,[{'name':'hair','box':[0,0,64,64]}])['regions'][0]
    assert r['reference_edge_energy']>r['candidate_edge_energy']
    assert r['edge_change_mean_u8']>0

@pytest.mark.parametrize('box',[[0,0,0,2],[-1,0,2,2],[0,0,65,64],[0.5,0,2,2]])
def test_no_clamped_regions(box):
    with pytest.raises(ValueError):compare(Image.new('RGB',(64,64)),Image.new('RGB',(64,64)),[{'name':'bad','box':box}])

def test_no_silent_resize():
    with pytest.raises(ValueError):compare(Image.new('RGB',(32,32)),Image.new('RGB',(64,64)),[])

def test_hidden_rgb_does_not_create_visible_error():
    a=Image.new('RGBA',(16,16),(255,0,0,0));b=Image.new('RGBA',(16,16),(0,255,0,0))
    assert compare(a,b,[])['whole']['max_error_u8']==0
    b.putpixel((0,0),(0,0,0,255));assert compare(a,b,[])['whole']['alpha_max_error_u8']==255

def test_profile_mismatch_is_not_silently_converted():
    a=Image.new('RGB',(16,16));b=a.copy();a.info['icc_profile']=b'one';b.info['icc_profile']=b'two'
    assert compare(a,b,[])['color_comparison']=='profile_mismatch'

def test_private_report_and_no_overwrite(tmp_path):
    a=tmp_path/'source.png';b=tmp_path/'render.png';Image.new('RGB',(16,16),'white').save(a);Image.new('RGB',(16,16),'gray').save(b)
    out=tmp_path/'review';write_review(a,b,[{'name':'<script>bad</script>','box':[0,0,8,8]}],out)
    html=(out/'index.html').read_text();report=json.loads((out/'report.json').read_text())
    assert '<script>bad</script>' not in html and '&lt;script&gt;' in html
    assert 'data:image/png;base64,' in html and 'https://' not in html
    assert report['inputs']['reference']['sha256']!=report['inputs']['candidate']['sha256']
    assert 'not_reviewed' in html
    with pytest.raises(FileExistsError):write_review(a,b,[],out)

def test_sixteen_bit_rgb_is_rejected_before_pillow_discards_precision(tmp_path):
    import struct,zlib
    def png(value):
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,16,2,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+struct.pack('>HHH',value,value,value)))+chunk(b'IEND',b'')
    a=tmp_path/'a.png';b=tmp_path/'b.png';a.write_bytes(png(32768));b.write_bytes(png(32769))
    with pytest.raises(ValueError,match='8-bit'):write_review(a,b,[],tmp_path/'out')

def test_literal_template_words_stay_region_names(tmp_path):
    a=tmp_path/'a.png';Image.new('RGB',(8,8),'white').save(a)
    write_review(a,a,[{'name':'REPORT','box':[0,0,4,4]}],tmp_path/'out')
    assert '<h2>REPORT</h2>' in (tmp_path/'out/index.html').read_text()
