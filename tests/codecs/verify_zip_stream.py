"""Independent stdlib decoder for test_zip_stream output; not an ORA/PNG test."""
import struct
import sys
import zipfile
from pathlib import Path

path = Path(sys.argv[1])
raw = path.read_bytes()
with zipfile.ZipFile(path) as archive:
    assert archive.testzip() is None
    assert archive.namelist() == ["mimetype", "data/test.txt", "data/彩色.bin", "empty"]
    assert archive.read("mimetype") == b"image/openraster"
    assert archive.read("data/test.txt") == b"hello"
    assert archive.read("data/彩色.bin") == bytes(i % 251 for i in range(200000))
    assert archive.read("empty") == b""
    for i, item in enumerate(archive.infolist()):
        fields = struct.unpack_from("<IHHHHHIIIHH", raw, item.header_offset)
        signature, version, flags, method, _, _, crc, compressed, size, name_n, extra_n = fields
        assert signature == 0x04034B50 and version == 20 and method == 0
        assert item.date_time == (1980, 1, 1, 0, 0, 0)
        assert flags == item.flag_bits == (0x800 if i == 0 else 0x808)
        start = item.header_offset + 30 + name_n + extra_n
        assert raw[item.header_offset + 30:item.header_offset + 30 + name_n].decode("utf-8") == item.filename
        if i == 0:
            assert item.header_offset == 0
            assert crc == item.CRC and compressed == size == item.file_size
        else:
            assert crc == compressed == size == 0
            desc = struct.unpack_from("<IIII", raw, start + item.compress_size)
            assert desc == (0x08074B50, item.CRC, item.compress_size, item.file_size)
    end = struct.unpack_from("<IHHHHIIH", raw, len(raw) - 22)
    assert end[0] == 0x06054B50 and end[3] == end[4] == 4
    assert end[5] + end[6] == len(raw) - 22
print("independent ZIP CRC, descriptor, offsets, UTF-8, and payload validation passed")
