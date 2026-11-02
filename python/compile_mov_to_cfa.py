#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import zlib
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

DEFAULT_MOV_DIR = REPO_ROOT / "assets_src" / "anim_mov"
DEFAULT_OUTPUT_DIR = REPO_ROOT / ".compile_mov_to_cfa"
DEFAULT_ASSETS_SDCARD_DIR = REPO_ROOT / "assets_sdcard"
DEFAULT_SD_ROOT = REPO_ROOT / "sdcard_root"

BOOK_SOURCE_GLOB = "book_*.mov"
CLEAN_SUFFIX = "_clean"

CFA_MAGIC = b"CFA1"
CFA_VERSION = 1
CFA_HEADER_FORMAT = "<4sHHHHIIIIIIII"
CFA_FRAME_FORMAT = "<HHHHHHBBIIIIH"
CFA_HEADER_SIZE = struct.calcsize(CFA_HEADER_FORMAT)
CFA_FRAME_SIZE = struct.calcsize(CFA_FRAME_FORMAT)

CFA_CODEC_RAW = 0
CFA_CODEC_RLE24 = 1
CFA_CODEC_LZ4_BLOCK = 2

CFA_FRAME_KEYFRAME = 1 << 0
CFA_FRAME_CLEAR_BEFORE_DRAW = 1 << 1
CFA_FRAME_EMPTY = 1 << 2

KEYFRAME_INTERVAL = 12
KEYFRAME_AREA_THRESHOLD = 0.70
RAW_MIN_SAVINGS_NUM = 9
RAW_MIN_SAVINGS_DEN = 10


@dataclass
class SourceInfo:
    path: str
    width: int
    height: int
    frame_count: int
    duration_s: float
    fps: float
    delay_ms: int


@dataclass
class FrameRecord:
    delay_ms: int
    x: int
    y: int
    w: int
    h: int
    flags: int
    codec: int
    raw_size: int
    payload_size: int
    payload_offset: int
    payload_crc32: int


@dataclass
class FrameReport:
    index: int
    delay_ms: int
    x: int
    y: int
    w: int
    h: int
    flags: int
    codec: str
    raw_size: int
    payload_size: int
    dirty_area: int


@dataclass
class CompileReport:
    source: SourceInfo
    output_cfa: str
    output_json: str
    assets_output_cfa: str | None
    sd_output_cfa: str | None
    total_payload_bytes: int
    file_size_bytes: int
    max_payload_size: int
    max_rect_pixels: int
    average_dirty_area: float
    codec_histogram: dict[str, int]
    frames: list[FrameReport]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile transparent MOV animations to Crow Frame Animation (.cfa)."
    )
    parser.add_argument("--mov-dir", type=Path, default=DEFAULT_MOV_DIR)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--assets-sdcard-dir", type=Path, default=DEFAULT_ASSETS_SDCARD_DIR)
    parser.add_argument("--sd-root", type=Path, default=DEFAULT_SD_ROOT)
    parser.add_argument("--only", default=None, help="Optional source stem to compile.")
    parser.add_argument(
        "--alpha-mode",
        choices=("straight", "premultiplied"),
        default="straight",
        help="How to interpret RGB channels in transparent pixels.",
    )
    parser.add_argument("--no-assets-copy", action="store_true", help="Do not stage .cfa files into assets_sdcard/.")
    parser.add_argument("--no-sd-copy", action="store_true", help="Do not stage .cfa files under <sd-root>/assets.")
    parser.add_argument("--no-lz4", action="store_true", help="Disable LZ4_BLOCK candidate generation.")
    parser.add_argument("--keep-existing", action="store_true", help="Do not remove previous output directory.")
    parser.add_argument(
        "--book-fps",
        type=float,
        default=None,
        help="Downsample book_* sources to this playback FPS by dropping frames.",
    )
    return parser.parse_args()


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"required tool is not available: {name}")
    return path


def run_json(cmd: list[str]) -> dict:
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return json.loads(result.stdout or "{}")


def parse_fraction(value: str | None) -> float | None:
    if not value or value == "0/0":
        return None
    if "/" not in value:
        return float(value)
    num, den = value.split("/", 1)
    den_f = float(den)
    return float(num) / den_f if den_f else None


def probe_source(ffprobe: str, source: Path) -> SourceInfo:
    payload = run_json(
        [
            ffprobe,
            "-v",
            "error",
            "-count_frames",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,nb_read_frames,nb_frames,r_frame_rate,avg_frame_rate",
            "-show_entries",
            "format=duration",
            "-of",
            "json",
            str(source),
        ]
    )
    stream = (payload.get("streams") or [{}])[0]
    fmt = payload.get("format") or {}
    width = int(stream["width"])
    height = int(stream["height"])
    raw_count = stream.get("nb_read_frames") or stream.get("nb_frames")
    frame_count = int(raw_count) if raw_count not in (None, "N/A") else 0
    duration_s = float(fmt.get("duration") or 0)
    fps = parse_fraction(stream.get("avg_frame_rate")) or parse_fraction(stream.get("r_frame_rate")) or 24.0
    if frame_count <= 0 and duration_s > 0:
        frame_count = int(round(duration_s * fps))
    if frame_count <= 0:
        raise RuntimeError(f"unable to determine frame count for {source}")
    delay_ms = max(1, int(round(1000.0 / fps)))
    return SourceInfo(
        path=str(source),
        width=width,
        height=height,
        frame_count=frame_count,
        duration_s=duration_s,
        fps=fps,
        delay_ms=delay_ms,
    )


def extract_rgba_frames(ffmpeg: str, source: Path, info: SourceInfo) -> Iterable[bytes]:
    frame_size = info.width * info.height * 4
    proc = subprocess.Popen(
        [
            ffmpeg,
            "-v",
            "error",
            "-i",
            str(source),
            "-an",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgba",
            "-",
        ],
        stdout=subprocess.PIPE,
    )
    assert proc.stdout is not None
    try:
        while True:
            frame = proc.stdout.read(frame_size)
            if not frame:
                break
            if len(frame) != frame_size:
                raise RuntimeError(f"short frame from ffmpeg for {source}: {len(frame)} != {frame_size}")
            yield frame
    finally:
        if proc.stdout:
            proc.stdout.close()
        return_code = proc.wait()
        if return_code != 0:
            raise RuntimeError(f"ffmpeg failed for {source} with exit code {return_code}")


def downsample_source_info(info: SourceInfo, target_fps: float | None) -> tuple[SourceInfo, int]:
    if not target_fps or target_fps <= 0.0 or target_fps >= info.fps:
        return info, 1

    stride = max(1, int(round(info.fps / target_fps)))
    output_fps = info.fps / stride
    output_frame_count = (info.frame_count + stride - 1) // stride
    return (
        SourceInfo(
            path=info.path,
            width=info.width,
            height=info.height,
            frame_count=output_frame_count,
            duration_s=info.duration_s,
            fps=output_fps,
            delay_ms=max(1, int(round(1000.0 / output_fps))),
        ),
        stride,
    )


def downsample_frames(frames: Iterable[bytes], stride: int) -> Iterable[bytes]:
    if stride <= 1:
        yield from frames
        return

    for source_index, frame in enumerate(frames):
        if (source_index % stride) == 0:
            yield frame


def rgba_to_rgb565a8_planes(rgba: bytes, width: int, height: int, alpha_mode: str) -> tuple[bytearray, bytearray]:
    pixel_count = width * height
    rgb565 = bytearray(pixel_count * 2)
    alpha = bytearray(pixel_count)
    src = memoryview(rgba)

    for px in range(pixel_count):
        src_off = px * 4
        r = int(src[src_off])
        g = int(src[src_off + 1])
        b = int(src[src_off + 2])
        a = int(src[src_off + 3])
        alpha[px] = a

        if a == 0:
            value = 0
        else:
            if alpha_mode == "premultiplied" and a < 255:
                r = min(255, (r * 255 + (a // 2)) // a)
                g = min(255, (g * 255 + (a // 2)) // a)
                b = min(255, (b * 255 + (a // 2)) // a)
            value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

        dst_off = px * 2
        rgb565[dst_off] = value & 0xFF
        rgb565[dst_off + 1] = (value >> 8) & 0xFF

    return rgb565, alpha


def diff_bbox(
    current_rgb: bytearray,
    current_alpha: bytearray,
    previous_rgb: bytearray | None,
    previous_alpha: bytearray | None,
    width: int,
    height: int,
) -> tuple[int, int, int, int] | None:
    if previous_rgb is None or previous_alpha is None:
        return visible_bbox(current_alpha, width, height)

    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    pixel_count = width * height

    for px in range(pixel_count):
        alpha_changed = current_alpha[px] != previous_alpha[px]
        rgb_off = px * 2
        rgb_changed = (
            current_alpha[px] != 0
            and (current_rgb[rgb_off] != previous_rgb[rgb_off] or current_rgb[rgb_off + 1] != previous_rgb[rgb_off + 1])
        )
        if alpha_changed or rgb_changed:
            y, x = divmod(px, width)
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
            if y < min_y:
                min_y = y
            if y > max_y:
                max_y = y

    if max_x < min_x:
        return None
    return min_x, min_y, max_x - min_x + 1, max_y - min_y + 1


def visible_bbox(alpha: bytearray, width: int, height: int) -> tuple[int, int, int, int] | None:
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1

    for px, a in enumerate(alpha):
        if a == 0:
            continue
        y, x = divmod(px, width)
        if x < min_x:
            min_x = x
        if x > max_x:
            max_x = x
        if y < min_y:
            min_y = y
        if y > max_y:
            max_y = y

    if max_x < min_x:
        return None
    return min_x, min_y, max_x - min_x + 1, max_y - min_y + 1


def rect_payload(rgb565: bytearray, alpha: bytearray, canvas_w: int, rect: tuple[int, int, int, int]) -> bytes:
    x, y, w, h = rect
    pixel_count = w * h
    rgb_plane = bytearray(pixel_count * 2)
    alpha_plane = bytearray(pixel_count)

    for row in range(h):
        src_px = (y + row) * canvas_w + x
        dst_px = row * w
        rgb_plane[dst_px * 2 : (dst_px + w) * 2] = rgb565[src_px * 2 : (src_px + w) * 2]
        alpha_plane[dst_px : dst_px + w] = alpha[src_px : src_px + w]

    return bytes(rgb_plane + alpha_plane)


def encode_rle24(raw: bytes, pixel_count: int) -> bytes:
    if pixel_count == 0:
        return b""

    rgb_size = pixel_count * 2
    out = bytearray()
    px = 0
    while px < pixel_count:
        rgb_off = px * 2
        rgb0 = raw[rgb_off]
        rgb1 = raw[rgb_off + 1]
        a = raw[rgb_size + px]
        run = 1
        while px + run < pixel_count and run < 0xFFFF:
            next_rgb = (px + run) * 2
            if raw[next_rgb] != rgb0 or raw[next_rgb + 1] != rgb1 or raw[rgb_size + px + run] != a:
                break
            run += 1
        out.extend(struct.pack("<HBBB", run, rgb0, rgb1, a))
        px += run
    return bytes(out)


def decode_rle24(encoded: bytes, raw_size: int) -> bytes:
    if raw_size % 3 != 0:
        raise ValueError("RGB565A8 raw size must be divisible by 3")
    pixel_count = raw_size // 3
    rgb_size = pixel_count * 2
    out = bytearray(raw_size)
    src = 0
    px = 0
    while src < len(encoded):
        if src + 5 > len(encoded):
            raise ValueError("truncated RLE24 run")
        run, rgb0, rgb1, a = struct.unpack_from("<HBBB", encoded, src)
        src += 5
        if run == 0 or px + run > pixel_count:
            raise ValueError("invalid RLE24 run length")
        for i in range(run):
            rgb_off = (px + i) * 2
            out[rgb_off] = rgb0
            out[rgb_off + 1] = rgb1
            out[rgb_size + px + i] = a
        px += run
    if px != pixel_count:
        raise ValueError("RLE24 did not fill output")
    return bytes(out)


def _emit_lz4_len(out: bytearray, length: int) -> int:
    nibble = min(length, 15)
    if length >= 15:
        length -= 15
        while length >= 255:
            out.append(255)
            length -= 255
        out.append(length)
    return nibble


def encode_lz4_block(data: bytes) -> bytes:
    n = len(data)
    if n == 0:
        return b""

    out = bytearray()
    table = [-1] * 65536
    anchor = 0
    i = 0

    while i + 4 <= n:
        seq = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16) | (data[i + 3] << 24)
        slot = ((seq * 2654435761) >> 16) & 0xFFFF
        ref = table[slot]
        table[slot] = i

        if ref >= 0 and i - ref <= 0xFFFF and data[ref : ref + 4] == data[i : i + 4]:
            match_len = 4
            while i + match_len < n and data[ref + match_len] == data[i + match_len]:
                match_len += 1

            literal_len = i - anchor
            token_pos = len(out)
            out.append(0)
            literal_nibble = _emit_lz4_len(out, literal_len)
            out[token_pos] = literal_nibble << 4
            out.extend(data[anchor:i])
            out.extend(struct.pack("<H", i - ref))

            match_code = match_len - 4
            match_nibble = _emit_lz4_len(out, match_code)
            out[token_pos] |= match_nibble

            i += match_len
            anchor = i
            continue

        i += 1

    literal_len = n - anchor
    token_pos = len(out)
    out.append(0)
    literal_nibble = _emit_lz4_len(out, literal_len)
    out[token_pos] = literal_nibble << 4
    out.extend(data[anchor:])
    return bytes(out)


def decode_lz4_block(encoded: bytes, raw_size: int) -> bytes:
    out = bytearray()
    src = 0
    encoded_len = len(encoded)

    while src < encoded_len:
        token = encoded[src]
        src += 1

        literal_len = token >> 4
        if literal_len == 15:
            while True:
                if src >= encoded_len:
                    raise ValueError("truncated LZ4 literal length")
                extra = encoded[src]
                src += 1
                literal_len += extra
                if extra != 255:
                    break

        if src + literal_len > encoded_len:
            raise ValueError("truncated LZ4 literals")
        out.extend(encoded[src : src + literal_len])
        src += literal_len
        if src >= encoded_len:
            break

        if src + 2 > encoded_len:
            raise ValueError("truncated LZ4 offset")
        offset = encoded[src] | (encoded[src + 1] << 8)
        src += 2
        if offset == 0 or offset > len(out):
            raise ValueError("invalid LZ4 offset")

        match_len = (token & 0x0F) + 4
        if (token & 0x0F) == 15:
            while True:
                if src >= encoded_len:
                    raise ValueError("truncated LZ4 match length")
                extra = encoded[src]
                src += 1
                match_len += extra
                if extra != 255:
                    break

        ref = len(out) - offset
        for _ in range(match_len):
            out.append(out[ref])
            ref += 1

    if len(out) != raw_size:
        raise ValueError(f"LZ4 output size mismatch: {len(out)} != {raw_size}")
    return bytes(out)


def choose_payload(raw: bytes, pixel_count: int, enable_lz4: bool) -> tuple[int, bytes]:
    candidates: list[tuple[int, bytes]] = [(CFA_CODEC_RAW, raw)]
    candidates.append((CFA_CODEC_RLE24, encode_rle24(raw, pixel_count)))
    if enable_lz4:
        candidates.append((CFA_CODEC_LZ4_BLOCK, encode_lz4_block(raw)))

    codec, payload = min(candidates, key=lambda item: len(item[1]))
    if codec != CFA_CODEC_RAW and len(payload) * RAW_MIN_SAVINGS_DEN > len(raw) * RAW_MIN_SAVINGS_NUM:
        return CFA_CODEC_RAW, raw
    return codec, payload


def codec_name(codec: int) -> str:
    return {
        CFA_CODEC_RAW: "RAW",
        CFA_CODEC_RLE24: "RLE24",
        CFA_CODEC_LZ4_BLOCK: "LZ4_BLOCK",
    }.get(codec, f"UNKNOWN_{codec}")


def decode_payload(codec: int, payload: bytes, raw_size: int) -> bytes:
    if codec == CFA_CODEC_RAW:
        if len(payload) != raw_size:
            raise ValueError("RAW payload size mismatch")
        return payload
    if codec == CFA_CODEC_RLE24:
        return decode_rle24(payload, raw_size)
    if codec == CFA_CODEC_LZ4_BLOCK:
        return decode_lz4_block(payload, raw_size)
    raise ValueError(f"unsupported codec {codec}")


def pack_header(
    width: int,
    height: int,
    frame_count: int,
    payload_offset: int,
    max_payload_size: int,
    max_rect_pixels: int,
    file_size: int,
) -> bytes:
    without_crc = struct.pack(
        CFA_HEADER_FORMAT,
        CFA_MAGIC,
        CFA_VERSION,
        CFA_HEADER_SIZE,
        width,
        height,
        frame_count,
        0,
        CFA_HEADER_SIZE,
        payload_offset,
        max_payload_size,
        max_rect_pixels,
        file_size,
        0,
    )
    header_crc = zlib.crc32(without_crc[:-4]) & 0xFFFFFFFF
    return without_crc[:-4] + struct.pack("<I", header_crc)


def unpack_header(data: bytes) -> dict:
    if len(data) < CFA_HEADER_SIZE:
        raise ValueError("truncated CFA header")
    fields = struct.unpack_from(CFA_HEADER_FORMAT, data, 0)
    header = {
        "magic": fields[0],
        "version": fields[1],
        "header_size": fields[2],
        "width": fields[3],
        "height": fields[4],
        "frame_count": fields[5],
        "flags": fields[6],
        "index_offset": fields[7],
        "payload_offset": fields[8],
        "max_payload_size": fields[9],
        "max_rect_pixels": fields[10],
        "file_size": fields[11],
        "header_crc32": fields[12],
    }
    if header["magic"] != CFA_MAGIC or header["version"] != CFA_VERSION or header["header_size"] != CFA_HEADER_SIZE:
        raise ValueError("unsupported CFA header")
    expected_crc = zlib.crc32(data[: CFA_HEADER_SIZE - 4]) & 0xFFFFFFFF
    if expected_crc != header["header_crc32"]:
        raise ValueError("CFA header CRC mismatch")
    return header


def write_cfa(
    output_path: Path,
    info: SourceInfo,
    frame_records: list[FrameRecord],
    payloads: list[bytes],
    max_payload_size: int,
    max_rect_pixels: int,
) -> None:
    index_offset = CFA_HEADER_SIZE
    payload_offset = index_offset + (len(frame_records) * CFA_FRAME_SIZE)
    current_payload_offset = payload_offset

    packed_frames = bytearray()
    for record, payload in zip(frame_records, payloads, strict=True):
        record.payload_offset = current_payload_offset
        current_payload_offset += len(payload)
        packed_frames.extend(
            struct.pack(
                CFA_FRAME_FORMAT,
                record.delay_ms,
                record.x,
                record.y,
                record.w,
                record.h,
                record.flags,
                record.codec,
                0,
                record.raw_size,
                record.payload_size,
                record.payload_offset,
                record.payload_crc32,
                0,
            )
        )

    file_size = current_payload_offset
    header = pack_header(
        info.width,
        info.height,
        len(frame_records),
        payload_offset,
        max_payload_size,
        max_rect_pixels,
        file_size,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as fp:
        fp.write(header)
        fp.write(packed_frames)
        for payload in payloads:
            fp.write(payload)


def decode_cfa(path: Path) -> tuple[dict, list[FrameRecord], list[tuple[FrameRecord, bytes]]]:
    data = path.read_bytes()
    header = unpack_header(data)
    if header["file_size"] != len(data):
        raise ValueError("CFA file size mismatch")

    frame_records: list[FrameRecord] = []
    decoded: list[tuple[FrameRecord, bytes]] = []
    index = header["index_offset"]
    for _ in range(header["frame_count"]):
        values = struct.unpack_from(CFA_FRAME_FORMAT, data, index)
        record = FrameRecord(
            delay_ms=values[0],
            x=values[1],
            y=values[2],
            w=values[3],
            h=values[4],
            flags=values[5],
            codec=values[6],
            raw_size=values[8],
            payload_size=values[9],
            payload_offset=values[10],
            payload_crc32=values[11],
        )
        frame_records.append(record)
        index += CFA_FRAME_SIZE

        payload = data[record.payload_offset : record.payload_offset + record.payload_size]
        if (zlib.crc32(payload) & 0xFFFFFFFF) != record.payload_crc32:
            raise ValueError("CFA payload CRC mismatch")
        decoded.append((record, decode_payload(record.codec, payload, record.raw_size)))

    return header, frame_records, decoded


def compile_rgba_frames(
    frames: Iterable[bytes],
    info: SourceInfo,
    output_path: Path,
    alpha_mode: str,
    enable_lz4: bool,
) -> CompileReport:
    previous_rgb: bytearray | None = None
    previous_alpha: bytearray | None = None
    frame_records: list[FrameRecord] = []
    payloads: list[bytes] = []
    frame_reports: list[FrameReport] = []
    codec_histogram: Counter[str] = Counter()
    total_dirty_area = 0
    max_payload_size = 0
    max_rect_pixels = 0

    for frame_index, rgba in enumerate(frames):
        rgb565, alpha = rgba_to_rgb565a8_planes(rgba, info.width, info.height, alpha_mode)
        diff_rect = diff_bbox(rgb565, alpha, previous_rgb, previous_alpha, info.width, info.height)
        force_keyframe = (
            frame_index == 0
            or frame_index % KEYFRAME_INTERVAL == 0
            or (
                diff_rect is not None
                and (diff_rect[2] * diff_rect[3]) >= int(info.width * info.height * KEYFRAME_AREA_THRESHOLD)
            )
        )
        rect = visible_bbox(alpha, info.width, info.height) if force_keyframe else diff_rect
        flags = 0
        if force_keyframe:
            flags |= CFA_FRAME_KEYFRAME | CFA_FRAME_CLEAR_BEFORE_DRAW

        if rect is None:
            flags |= CFA_FRAME_EMPTY
            raw = b""
            payload = b""
            codec = CFA_CODEC_RAW
            x = y = w = h = 0
        else:
            x, y, w, h = rect
            raw = rect_payload(rgb565, alpha, info.width, rect)
            codec, payload = choose_payload(raw, w * h, enable_lz4)

        raw_size = len(raw)
        payload_crc = zlib.crc32(payload) & 0xFFFFFFFF
        payload_size = len(payload)
        max_payload_size = max(max_payload_size, payload_size)
        max_rect_pixels = max(max_rect_pixels, w * h)
        total_dirty_area += w * h
        codec_histogram[codec_name(codec)] += 1

        frame_records.append(
            FrameRecord(
                delay_ms=info.delay_ms,
                x=x,
                y=y,
                w=w,
                h=h,
                flags=flags,
                codec=codec,
                raw_size=raw_size,
                payload_size=payload_size,
                payload_offset=0,
                payload_crc32=payload_crc,
            )
        )
        payloads.append(payload)
        frame_reports.append(
            FrameReport(
                index=frame_index,
                delay_ms=info.delay_ms,
                x=x,
                y=y,
                w=w,
                h=h,
                flags=flags,
                codec=codec_name(codec),
                raw_size=raw_size,
                payload_size=payload_size,
                dirty_area=w * h,
            )
        )
        previous_rgb = rgb565
        previous_alpha = alpha

    if len(frame_records) != info.frame_count:
        raise RuntimeError(f"decoded {len(frame_records)} frames from {info.path}, expected {info.frame_count}")

    write_cfa(output_path, info, frame_records, payloads, max_payload_size, max_rect_pixels)
    json_path = output_path.with_suffix(".json")
    report = CompileReport(
        source=info,
        output_cfa=str(output_path),
        output_json=str(json_path),
        assets_output_cfa=None,
        sd_output_cfa=None,
        total_payload_bytes=sum(len(payload) for payload in payloads),
        file_size_bytes=output_path.stat().st_size,
        max_payload_size=max_payload_size,
        max_rect_pixels=max_rect_pixels,
        average_dirty_area=(total_dirty_area / len(frame_records)) if frame_records else 0.0,
        codec_histogram=dict(codec_histogram),
        frames=frame_reports,
    )
    json_path.write_text(json.dumps(asdict(report), indent=2), encoding="utf-8")

    header, decoded_records, decoded_payloads = decode_cfa(output_path)
    if header["frame_count"] != len(frame_records) or len(decoded_records) != len(frame_records):
        raise RuntimeError("CFA roundtrip frame count mismatch")
    for record, decoded in decoded_payloads:
        expected_raw_size = record.w * record.h * 3
        if record.raw_size != expected_raw_size or len(decoded) != expected_raw_size:
            raise RuntimeError("CFA roundtrip raw-size mismatch")

    return report


def collect_sources(mov_dir: Path, only: str | None) -> list[Path]:
    sources = sorted(path for path in mov_dir.glob(BOOK_SOURCE_GLOB) if path.is_file())

    if only:
        normalized_only = only.removesuffix(".mov")
        sources = [
            path
            for path in sources
            if path.stem == normalized_only
            or path.stem.removesuffix(CLEAN_SUFFIX) == normalized_only
        ]

    if not sources:
        raise RuntimeError(f"no book MOV sources found in {mov_dir} matching {BOOK_SOURCE_GLOB}")

    return sources


def collect_reports(output_dir: Path, updated_reports: list[CompileReport], merge_existing: bool) -> list[CompileReport]:
    reports_by_source = {report.source.path: report for report in updated_reports}

    if merge_existing:
        for report_path in sorted(output_dir.glob("*.json")):
            if report_path.name == "summary.json":
                continue
            try:
                payload = json.loads(report_path.read_text(encoding="utf-8"))
                report = CompileReport(
                    source=SourceInfo(**payload["source"]),
                    output_cfa=payload["output_cfa"],
                    output_json=payload["output_json"],
                    assets_output_cfa=payload.get("assets_output_cfa"),
                    sd_output_cfa=payload.get("sd_output_cfa"),
                    total_payload_bytes=payload["total_payload_bytes"],
                    file_size_bytes=payload["file_size_bytes"],
                    max_payload_size=payload["max_payload_size"],
                    max_rect_pixels=payload["max_rect_pixels"],
                    average_dirty_area=payload["average_dirty_area"],
                    codec_histogram=dict(payload["codec_histogram"]),
                    frames=[FrameReport(**frame) for frame in payload["frames"]],
                )
            except (KeyError, TypeError, ValueError, json.JSONDecodeError):
                continue
            reports_by_source.setdefault(report.source.path, report)

    return sorted(reports_by_source.values(), key=lambda report: Path(report.source.path).name)


def main() -> int:
    args = parse_args()
    ffprobe = require_tool("ffprobe")
    ffmpeg = require_tool("ffmpeg")

    if args.output_dir.exists() and not args.keep_existing:
        shutil.rmtree(args.output_dir)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    assets_sdcard_dir: Path | None = None
    if not args.no_assets_copy:
        assets_sdcard_dir = args.assets_sdcard_dir
        assets_sdcard_dir.mkdir(parents=True, exist_ok=True)

    sd_assets_dir: Path | None = None
    if not args.no_sd_copy:
        sd_assets_dir = args.sd_root / "assets"
        sd_assets_dir.mkdir(parents=True, exist_ok=True)

    reports: list[CompileReport] = []
    for source in collect_sources(args.mov_dir, args.only):
        source_info = probe_source(ffprobe, source)
        info, frame_stride = downsample_source_info(
            source_info,
            args.book_fps if source.stem.startswith("book_") else None,
        )
        output_stem = source.stem.removesuffix(CLEAN_SUFFIX)
        output_path = args.output_dir / f"{output_stem}.cfa"
        print(
            f"Compiling {source.name}: {source_info.width}x{source_info.height} "
            f"frames={source_info.frame_count}->{info.frame_count} "
            f"fps={source_info.fps:.3f}->{info.fps:.3f} stride={frame_stride}"
        )
        report = compile_rgba_frames(
            downsample_frames(extract_rgba_frames(ffmpeg, source, source_info), frame_stride),
            info,
            output_path,
            args.alpha_mode,
            enable_lz4=not args.no_lz4,
        )
        if assets_sdcard_dir:
            assets_output = assets_sdcard_dir / output_path.name
            if assets_output.resolve() != output_path.resolve():
                shutil.copy2(output_path, assets_output)
            report.assets_output_cfa = str(assets_output)
        if sd_assets_dir:
            sd_output = sd_assets_dir / output_path.name
            if sd_output.resolve() != output_path.resolve():
                shutil.copy2(output_path, sd_output)
            report.sd_output_cfa = str(sd_output)
        report_path = output_path.with_suffix(".json")
        report_path.write_text(json.dumps(asdict(report), indent=2), encoding="utf-8")
        reports.append(report)
        print(
            f"  -> {output_path} size={report.file_size_bytes} "
            f"payload={report.total_payload_bytes} codecs={report.codec_histogram}"
        )

    summary_reports = collect_reports(args.output_dir, reports, merge_existing=args.keep_existing)
    summary_path = args.output_dir / "summary.json"
    summary_path.write_text(json.dumps([asdict(report) for report in summary_reports], indent=2), encoding="utf-8")
    print(f"Summary written to {summary_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
