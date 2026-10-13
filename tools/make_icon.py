"""Package the generated PNG as a Windows icon; no image generation occurs here."""
from pathlib import Path
from PIL import Image


def main():
    project = Path(__file__).resolve().parent.parent
    source = project / "assets" / "plume.png"
    destination = project / "assets" / "plume.ico"
    with Image.open(source) as image:
        image.convert("RGBA").save(
            destination,
            format="ICO",
            sizes=[(size, size) for size in (16, 24, 32, 48, 64, 128, 256)],
        )
    print(destination)


if __name__ == "__main__":
    main()
