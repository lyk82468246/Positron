"""Fixed AVC parameter/framing changes; no general muxer or product code."""
CASES = {"repeat": "baseline-aac.mp4", "vga": "main-vga.mp4",
         "oversize": "oversize.mp4", "high": "high.mp4",
         "high422": "high422.mp4", "interlaced": "interlaced.mp4"}


def boxes(data, start, end):
    while start < end:
        size = int.from_bytes(data[start:start + 4], "big")
        assert size >= 8 and start + size <= end
        yield start, size, data[start + 4:start + 8]
        start += size
    assert start == end


def atom(kind, payload):
    return (len(payload) + 8).to_bytes(4, "big") + kind + payload


def words(values):
    return b"".join(v.to_bytes(4, "big") for v in values)


def u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def track(data, kind):
    def child(box, name):
        return next(b for b in boxes(data, box[0] + 8, box[0] + box[1]) if b[2] == name)
    moov = next(b for b in boxes(data, 0, len(data)) if b[2] == b"moov")
    for box in boxes(data, moov[0] + 8, moov[0] + moov[1]):
        if box[2] == b"trak":
            mdia = child(box, b"mdia")
            handler = child(mdia, b"hdlr")
            if data[handler[0] + 16:handler[0] + 20] == kind:
                stbl = child(child(mdia, b"minf"), b"stbl")
                return moov, box, mdia, stbl, child
    raise AssertionError("Missing fixed track")


def video(data):
    unused, unused2, mdia, stbl, child = track(data, b"vide")
    mdhd = child(mdia, b"mdhd")
    assert u32(data, mdhd[0] + 20) == 10240
    tables = {b[2]: data[b[0] + 8:b[0] + b[1]] for b in boxes(data, stbl[0] + 8, stbl[0] + stbl[1])}
    sizes = [u32(tables[b"stsz"], i) for i in range(12, len(tables[b"stsz"]), 4)]
    assert len(sizes) == 3 and tables[b"stts"] == words((0, 1, 3, 2048))
    offsets = [u32(tables[b"stco"], i) for i in range(8, len(tables[b"stco"]), 4)]
    stsc = tables[b"stsc"]
    runs = [tuple(u32(stsc, i + j) for j in (0, 4, 8)) for i in range(8, len(stsc), 12)]
    samples = []
    for number, offset in enumerate(offsets, 1):
        run = next(r for r in reversed(runs) if r[0] <= number)
        for unused in range(run[1]):
            size = sizes[len(samples)]
            samples.append(data[offset:offset + size])
            offset += size
    assert len(samples) == 3
    ctts = tables.get(b"ctts", words((0, 1, 3, 0)))
    shifts = [u32(ctts, i + 4) for i in range(8, len(ctts), 8) for unused in range(u32(ctts, i))]
    origin = min(i * 2048 + shift for i, shift in enumerate(shifts))
    shifts = [shift - origin + 4096 for shift in shifts]
    entry = tables[b"stsd"][8:]
    avcc = next(b for b in boxes(entry, 86, len(entry)) if b[2] == b"avcC")
    config = entry[avcc[0] + 8:avcc[0] + avcc[1]]
    assert config[0] == 1 and config[4] & 3 == 3
    position = 6
    headers = []
    for count in (config[5] & 31, None):
        if count is None:
            count = config[position]
            position += 1
        for unused in range(count):
            size = int.from_bytes(config[position:position + 2], "big")
            position += 2
            headers.append(size.to_bytes(4, "big") + config[position:position + size])
            position += size
    return samples, shifts, b"".join(headers), tables[b"stsd"]


def short_lengths(sample):
    """Fixed four-to-two byte framing; retain every complete NAL payload."""
    result = bytearray()
    position = 0
    while position < len(sample):
        size = u32(sample, position)
        position += 4
        assert 0 < size < 65536 and position + size <= len(sample)
        result += size.to_bytes(2, "big") + sample[position:position + size]
        position += size
    assert position == len(sample)
    return bytes(result)


def records(destination, external=False, nal_switch=False, returning=False):
    assert not (nal_switch and returning)
    external = external or nal_switch
    original = (destination / "baseline-aac.mp4").read_bytes()
    top = list(boxes(original, 0, len(original)))
    mdat = next(b for b in top if b[2] == b"mdat")
    moov, vtrack, mdia, stbl, child = track(original, b"vide")
    unused, unused2, unused3, audio_stbl, audio_child = track(original, b"soun")
    audio_stco = audio_child(audio_stbl, b"stco")
    first, first_shifts, first_headers, stsd = video(original)
    result = []
    for stem, source in CASES.items():
        if returning and stem not in ("repeat", "vga"):
            continue
        second, second_shifts, headers, second_stsd = video((destination / source).read_bytes())
        if not external or nal_switch:
            second[0] = headers + second[0]
        if nal_switch:
            second = [short_lengths(sample) for sample in second]
        samples = first + second
        shifts = first_shifts + second_shifts
        if returning:
            third = list(first)
            if not external:
                third[0] = first_headers + third[0]
            samples += third
            shifts += first_shifts
        count = len(samples)
        replacements = {}

        def header(box, offset, value):
            data = bytearray(original[box[0]:box[0] + box[1]])
            data[offset:offset + 4] = value.to_bytes(4, "big")
            replacements[box[0]] = bytes(data)

        header(child(mdia, b"mdhd"), 24, count * 2048)
        header(child(vtrack, b"tkhd"), 28, 400 + count * 200)
        header(child(moov, b"mvhd"), 24, 400 + count * 200)
        edts = child(vtrack, b"edts")
        replacements[edts[0]] = atom(b"edts", atom(b"elst", words((0, 2, 400, 0xffffffff, 65536,
                                                                     count * 200, 4096, 65536))))
        description = bytearray(stsd)
        assert description[12:16] == b"avc1"
        if nal_switch:
            description[12:16] = b"avc3"
            second_entry = bytearray(description[8:])
            avcc = next(b for b in boxes(second_entry, 86, len(second_entry)) if b[2] == b"avcC")
            second_entry[avcc[0] + 12] = 0xfd
            description[4:8] = words((2,))
            description += second_entry
        elif external:
            description[4:8] = words((2,))
            description += second_stsd[8:]
        else:
            description[12:16] = b"avc3"

        def table(start):
            offsets = []
            for sample in samples:
                offsets.append(start)
                start += len(sample)
            return atom(b"stbl", atom(b"stsd", bytes(description)) +
                        atom(b"stts", words((0, 1, count, 2048))) +
                        atom(b"ctts", words((0, count)) + b"".join(words((1, v)) for v in shifts)) +
                        atom(b"stsc", words((0, 3, 1, 1, 1, 4, 1, 2, 7, 1, 1) if external and returning else
                                            (0, 2, 1, 1, 1, 4, 1, 2) if external else
                                            (0, 1, 1, 1, 1))) +
                        atom(b"stss", words((0, 3, 1, 4, 7) if returning else (0, 2, 1, 4))) +
                        atom(b"stsz", words((0, 0, count)) + words([len(s) for s in samples])) +
                        atom(b"stco", words((0, count)) + words(offsets)))

        def rebuild(box):
            start, size, kind = box
            if start in replacements:
                return replacements[start]
            if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
                return atom(kind, b"".join(rebuild(b) for b in boxes(original, start + 8, start + size)))
            return original[start:start + size]

        replacements[stbl[0]] = table(0)
        growth = len(rebuild(moov)) - moov[1]
        replacements[stbl[0]] = table(mdat[0] + mdat[1] + growth)
        old_offsets = original[audio_stco[0] + 8:audio_stco[0] + audio_stco[1]]
        replacements[audio_stco[0]] = atom(b"stco", old_offsets[:8] +
            words([u32(old_offsets, i) + growth for i in range(8, len(old_offsets), 4)]))
        payload = original[mdat[0] + 8:mdat[0] + mdat[1]] + b"".join(samples)
        data = b"".join(atom(b"mdat", payload) if b[2] == b"mdat" else rebuild(b) for b in top)
        name = (("mixed-return-avcc-" if external else "mixed-return-inband-") if returning else
                "mixed-nal-" if nal_switch else "mixed-avcc-" if external else "mixed-video-") + stem + ".mp4"
        (destination / name).write_bytes(data)
        transform = (("fixed AVC1 description 1-2-1 return v1" if external else "fixed AVC3 in-band parameter return v1") if returning else
                     "fixed AVC3 avcC four-to-two-byte lengths plus in-band SPS/PPS v1" if nal_switch else
                     "fixed AVC1 stsd/stsc external avcC transition v1" if external else "fixed AVC3 in-band SPS/PPS transition v1")
        result.append({"file": name, "fixture_transform": transform,
                       "source": "baseline-aac.mp4", "video_source": source})
    return result
