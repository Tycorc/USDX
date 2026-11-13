"""Generate a real mid-stream display-size change without a large coded image."""

from pathlib import Path
import shutil
import subprocess
import tempfile


def make_failure_fixture(path):
    """Write MPEG-TS: 1.5 seconds at SAR 1:1, then 1.5 seconds at SAR 256:1.

    Both segments decode 320x256 constant saturated quadrants. The latter's
    display width is 81920, exceeding normal GPU texture limits. This exercises
    the renderer's allocation failure after playback starts, not a decoder error.
    Timestamps continue across the segments; concatenation preserves the SPS
    change rather than letting a remuxer normalize the aspect ratio.
    """
    path = Path(path)
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is needed to generate the mid-stream failure fixture")
    pattern = ("color=c=red:size=320x256:rate=24,"
               "drawbox=x=160:y=0:w=160:h=128:color=0x00ff00:t=fill,"
               "drawbox=x=0:y=128:w=160:h=128:color=blue:t=fill,"
               "drawbox=x=160:y=128:w=160:h=128:color=yellow:t=fill")
    with tempfile.TemporaryDirectory(prefix="video-sar-", dir=path.parent) as temporary:
        segments = []
        for index, sar in enumerate(("1/1", "256/1")):
            segment = Path(temporary) / f"segment-{index}.ts"
            subprocess.run([
                ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
                "-f", "lavfi", "-i", pattern, "-t", "1.5",
                "-vf", f"setsar={sar}:max=65535", "-an",
                "-c:v", "libx264", "-threads", "1", "-preset", "ultrafast",
                "-g", "12", "-bf", "0", "-pix_fmt", "yuv420p",
                "-muxdelay", "0", "-muxpreload", "0",
                "-output_ts_offset", str(index * 1.5), str(segment),
            ], check=True, timeout=30)
            segments.append(segment.read_bytes())
        path.write_bytes(b"".join(segments))
