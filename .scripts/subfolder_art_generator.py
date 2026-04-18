#!/usr/bin/env python3
"""
Generate random PNG artwork for subfolder/subcollection testing.

Scans a media directory for subfolders and creates a randomly colored PNG
for each one, saving them to an output directory with matching names.
Useful for testing subcollection artwork display in Kartend.
"""

import argparse
import os
import random
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)


def generate_random_color() -> tuple[int, int, int]:
    """Generate a random RGB color, avoiding very dark/light colors."""
    return (
        random.randint(40, 215),
        random.randint(40, 215),
        random.randint(40, 215),
    )


def generate_contrasting_color(bg_color: tuple[int, int, int]) -> tuple[int, int, int]:
    """Generate a contrasting text color for readability."""
    luminance = 0.299 * bg_color[0] + 0.587 * bg_color[1] + 0.114 * bg_color[2]
    return (255, 255, 255) if luminance < 128 else (0, 0, 0)


def create_artwork(
    folder_name: str,
    output_path: str,
    width: int = 256,
    height: int = 256,
) -> None:
    """Create a random colored PNG with the folder name as text."""
    bg_color = generate_random_color()
    text_color = generate_contrasting_color(bg_color)

    image = Image.new("RGB", (width, height), bg_color)
    draw = ImageDraw.Draw(image)

    # Try to use a reasonable font size
    font_size = max(12, min(width, height) // 8)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size)
    except (OSError, IOError):
        font = ImageFont.load_default()

    # Get text bounding box and center it
    bbox = draw.textbbox((0, 0), folder_name, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]

    # Wrap text if too wide
    if text_width > width - 20:
        # Simple word wrap
        words = folder_name.split()
        lines = []
        current_line = ""
        for word in words:
            test_line = f"{current_line} {word}".strip()
            bbox = draw.textbbox((0, 0), test_line, font=font)
            if bbox[2] - bbox[0] <= width - 20:
                current_line = test_line
            else:
                if current_line:
                    lines.append(current_line)
                current_line = word
        if current_line:
            lines.append(current_line)

        # Draw wrapped text
        total_height = len(lines) * (text_height + 5)
        y = (height - total_height) // 2
        for line in lines:
            bbox = draw.textbbox((0, 0), line, font=font)
            line_width = bbox[2] - bbox[0]
            x = (width - line_width) // 2
            draw.text((x, y), line, fill=text_color, font=font)
            y += text_height + 5
    else:
        x = (width - text_width) // 2
        y = (height - text_height) // 2
        draw.text((x, y), folder_name, fill=text_color, font=font)

    image.save(output_path, "PNG")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate random PNG artwork for subfolders",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s /path/to/media /path/to/artwork
  %(prog)s ~/Media ~/Artwork --size 512x512
  %(prog)s . ./covers --overwrite
        """,
    )
    parser.add_argument(
        "media_dir",
        help="Media directory containing subfolders to generate artwork for",
    )
    parser.add_argument(
        "output_dir",
        help="Output directory to save generated artwork",
    )
    parser.add_argument(
        "--size",
        default="256x256",
        help="Image size as WIDTHxHEIGHT (default: 256x256)",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing artwork files",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be created without creating files",
    )

    args = parser.parse_args()

    # Parse size
    try:
        width, height = map(int, args.size.lower().split("x"))
    except ValueError:
        print(f"Error: Invalid size format '{args.size}'. Use WIDTHxHEIGHT (e.g., 256x256)")
        sys.exit(1)

    media_dir = os.path.expanduser(args.media_dir)
    output_dir = os.path.expanduser(args.output_dir)

    if not os.path.isdir(media_dir):
        print(f"Error: Media directory does not exist: {media_dir}")
        sys.exit(1)

    # Get list of folders
    folders = [
        name for name in os.listdir(media_dir)
        if os.path.isdir(os.path.join(media_dir, name))
    ]

    if not folders:
        print(f"No folders found in {media_dir}")
        sys.exit(0)

    # Create output directory if needed
    if not args.dry_run:
        os.makedirs(output_dir, exist_ok=True)

    created = 0
    skipped = 0

    for folder in sorted(folders):
        output_path = os.path.join(output_dir, f"{folder}.png")

        if os.path.exists(output_path) and not args.overwrite:
            print(f"Skipping (exists): {folder}.png")
            skipped += 1
            continue

        if args.dry_run:
            print(f"Would create: {folder}.png")
            created += 1
        else:
            create_artwork(folder, output_path, width, height)
            print(f"Created: {folder}.png")
            created += 1

    print(f"\nDone: {created} created, {skipped} skipped")


if __name__ == "__main__":
    main()
