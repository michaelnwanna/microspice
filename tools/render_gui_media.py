import argparse
from pathlib import Path

from PIL import Image, ImageOps


def screenshot(source, destination):
    with Image.open(source) as capture:
        image = ImageOps.contain(capture.convert("RGB"), (1100, 800))
        image.save(destination, optimize=True)


def animation(sources, destination, ping_pong=False):
    frames = []
    for source in sources:
        with Image.open(source) as capture:
            frames.append(ImageOps.contain(capture.convert("RGB"), (1000, 800)))
    if len(frames) < 5 or len({frame.size for frame in frames}) != 1:
        raise ValueError("Animation needs at least five equal-sized GUI captures")
    if ping_pong:
        frames += frames[-2:0:-1]
    swatches = Image.new("RGB", (960, 120 * ((len(frames) + 5) // 6)), "white")
    for index, frame in enumerate(frames):
        swatches.paste(frame.resize((160, 120)), ((index % 6) * 160, (index // 6) * 120))
    palette = swatches.quantize(colors=240)
    interface_colors = (
        "#007f78", "#263b34", "#263638", "#ffffff",
        "#fcfcfc", "#f3f6f4", "#e8eeea", "#dce4df",
        "#d9ede7", "#7cb3a1", "#ad5431", "#45585b",
        "#397a63", "#ccd4d7", "#88958e", "#000000",
    )
    reserved = [channel for color in interface_colors for channel in
                tuple(bytes.fromhex(color[1:]))]
    palette.putpalette(palette.getpalette()[:720] + reserved)
    encoded = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    durations = [120] * len(encoded)
    durations[0] = 1400
    if ping_pong:
        durations[len(sources) - 1] = 1000
    else:
        durations[-2] = 1800
        durations[-1] = 1000
    encoded[0].save(
        destination,
        save_all=True,
        append_images=encoded[1:],
        duration=durations,
        loop=0,
        disposal=1,
        optimize=True,
    )
    with Image.open(destination) as result:
        if result.n_frames < 5 or result.info.get("loop") != 0:
            raise ValueError("Generated GIF is not a looping animation")
        for index in range(result.n_frames):
            result.seek(index)
            result.load()
    print(f"{destination.name}: {destination.stat().st_size:,} bytes")


def main():
    parser = argparse.ArgumentParser(description="Encode screenshots and GIFs from real Qt GUI captures")
    parser.add_argument("captures", type=Path)
    parser.add_argument("--script-only", action="store_true")
    parser.add_argument(
        "--output", type=Path, default=Path(__file__).resolve().parents[1] / "docs" / "images"
    )
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    if arguments.script_only:
        screenshot(
            arguments.captures / "script-workspace-desktop.png",
            arguments.output / "gui-script-workspace.png",
        )
        frames = sorted(arguments.captures.glob("demo-script-[0-9][0-9].png"))
        if len(frames) != 6:
            raise ValueError("Expected six script workflow capture frames")
        animation(frames, arguments.output / "gui-script-workspace.gif")
        return
    for scene in ("editor", "waveform", "inspector"):
        screenshot(
            arguments.captures / f"gui-demo-{scene}.png",
            arguments.output / f"gui-{scene}.png",
        )
    editing = sorted(arguments.captures.glob("demo-edit-[0-9][0-9].png"))
    waveform = sorted(arguments.captures.glob("demo-waveform-[0-9][0-9].png"))
    if len(editing) != 19 or len(waveform) != 25:
        raise ValueError("Expected 19 editing and 25 waveform capture frames")
    animation(editing, arguments.output / "gui-editing.gif")
    animation(waveform, arguments.output / "gui-waveform.gif", ping_pong=True)


if __name__ == "__main__":
    main()