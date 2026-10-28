"""Integer storage contracts shared by asset generation and host checks."""
def glyph(width, height, encoded_bytes):
    if not (0 <= width <= 255 and 0 <= height <= 255 and 1 <= encoded_bytes <= 65535):
        raise ValueError("glyph exceeds uint8 geometry or uint16 encoded-length storage")

def font(count, ascent, descent, start, end):
    if not (1 <= count <= 255 and 0 <= ascent <= 255 and 0 <= descent <= 255 and
            0 <= start <= end <= 255):
        raise ValueError("font exceeds target byte metadata storage")
