"""Fixed AAC sample-description MP4 fixtures, not a general muxer or product code."""
CASES = {"repeat": (0, 0), "stereo": (0, 1), "mono": (1, 0), "return": (0, 1, 0), "rate-down": (0, 2)}
SOURCES = ("aac-lc.aac", "aac-lc-stereo.aac", "aac-lc-44100.aac")
RATES = (48000, 48000, 44100)
CHANNELS = (1, 2, 1)


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
    return b"".join(value.to_bytes(4, "big") for value in values)


def units(data):
    result = []
    position = 0
    while position < len(data):
        header = data[position:position + 7]
        assert len(header) == 7 and header[:2] == b"\xff\xf1" and header[2] >> 6 == 1
        length = ((header[3] & 3) << 11) | (header[4] << 3) | (header[5] >> 5)
        assert length > 7 and position + length <= len(data) and header[6] & 3 == 0
        result.append(data[position + 7:position + length])
        position += length
    assert len(result) in (27, 30)
    return result


def records(destination):
    original = (destination / "mixed-clock-video-late.mp4").read_bytes()
    top = list(boxes(original, 0, len(original)))
    moov = next(box for box in top if box[2] == b"moov")
    mdat = next(box for box in top if box[2] == b"mdat")

    def child(box, kind):
        return next(b for b in boxes(original, box[0] + 8, box[0] + box[1]) if b[2] == kind)

    tracks = [box for box in boxes(original, moov[0] + 8, moov[0] + moov[1]) if box[2] == b"trak"]
    audio = next(t for t in tracks if original[child(child(t, b"mdia"), b"hdlr")[0] + 16:
                                             child(child(t, b"mdia"), b"hdlr")[0] + 20] == b"soun")
    video = next(t for t in tracks if t != audio)
    mdia = child(audio, b"mdia")
    stbl = child(child(mdia, b"minf"), b"stbl")
    stsd = child(stbl, b"stsd")
    entry = original[stsd[0] + 16:stsd[0] + stsd[1]]
    assert entry[4:8] == b"mp4a" and entry[16:20] == bytes(4)
    video_stco = child(child(child(child(video, b"mdia"), b"minf"), b"stbl"), b"stco")
    raw = [units((destination / name).read_bytes()) for name in SOURCES]

    def description(index):
        asc = (0x1188, 0x1190, 0x1208)[index].to_bytes(2, "big")
        def descriptor(tag, payload):
            assert len(payload) < 128
            return bytes((tag, len(payload))) + payload
        decoder = descriptor(4, b"\x40\x15" + bytes(11) + descriptor(5, asc))
        esds = atom(b"esds", bytes(4) + descriptor(3, b"\x00\x02\x00" + decoder + descriptor(6, b"\x02")))
        prefix = bytearray(entry[8:36])
        prefix[16:18] = CHANNELS[index].to_bytes(2, "big")
        prefix[24:28] = (RATES[index] << 16).to_bytes(4, "big")
        return atom(b"mp4a", bytes(prefix) + esds)

    result = []
    for stem, segments in CASES.items():
        samples = [sample for index in segments for sample in raw[index]]
        payload = original[mdat[0] + 8:mdat[0] + mdat[1]] + b"".join(samples)
        replacement = {}

        def header(box, offset, value):
            data = bytearray(replacement.get(box[0], original[box[0]:box[0] + box[1]]))
            data[offset:offset + 4] = value.to_bytes(4, "big")
            replacement[box[0]] = bytes(data)

        count = len(samples)
        scale = 7056000 if 2 in segments else 48000
        ticks = sum(len(raw[i]) * 1024 * scale // RATES[i] for i in segments)
        header(child(mdia, b"mdhd"), 20, scale)
        header(child(mdia, b"mdhd"), 24, ticks)
        duration = (ticks * 1000 + scale - 1) // scale
        header(child(audio, b"tkhd"), 28, duration)
        header(child(moov, b"mvhd"), 24, duration)
        edts = child(audio, b"edts")
        replacement[edts[0]] = atom(b"free", original[edts[0] + 8:edts[0] + edts[1]])

        def audio_table(start):
            chunk_offsets = []
            position = start
            for sample in samples:
                chunk_offsets.append(position)
                position += len(sample)
            descriptions = b"".join(description(index) for index in segments)
            timing = [(count, 1024)] if scale == 48000 else [(len(raw[i]), 1024 * scale // RATES[i]) for i in segments]
            return atom(b"stbl", atom(b"stsd", words((0, len(segments))) + descriptions) +
                        atom(b"stts", words((0, len(timing))) + b"".join(words(pair) for pair in timing)) +
                        atom(b"stsc", words((0, len(segments))) +
                             b"".join(words((sum(len(raw[j]) for j in segments[:i]) + 1, 1, i + 1)) for i in range(len(segments)))) +
                        atom(b"stsz", words((0, 0, count)) + words([len(s) for s in samples])) +
                        atom(b"stco", words((0, count)) + words(chunk_offsets)))

        def rebuild(box):
            start, size, kind = box
            if start in replacement:
                return replacement[start]
            if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl"):
                return atom(kind, b"".join(rebuild(b) for b in boxes(original, start + 8, start + size)))
            return original[start:start + size]

        replacement[stbl[0]] = audio_table(0)
        growth = len(rebuild(moov)) - moov[1]
        replacement[stbl[0]] = audio_table(mdat[0] + growth + mdat[1])
        offsets = original[video_stco[0] + 8:video_stco[0] + video_stco[1]]
        relocated = [int.from_bytes(offsets[i:i + 4], "big") + growth for i in range(8, len(offsets), 4)]
        replacement[video_stco[0]] = atom(b"stco", offsets[:8] + words(relocated))
        data = b"".join(rebuild(box) if box[2] != b"mdat" else atom(b"mdat", payload) for box in top)
        name = "mixed-config-" + stem + ".mp4"
        (destination / name).write_bytes(data)
        result.append({"file": name, "fixture_transform": "fixed MP4 AAC stsd/stsc transitions v1",
                       "source": "mixed-clock-video-late.mp4", "aac_sources": [SOURCES[i] for i in segments]})
    return result
