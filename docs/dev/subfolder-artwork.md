# Subfolder Artwork Generator

Generate placeholder artwork for subcollections and subfolders.

## Overview

The subfolder artwork generator scans a media directory for subfolders and creates a randomly colored PNG for each one. The generated images are saved to an output directory with filenames matching the subfolder names.

This is useful for testing Kartend's subcollection artwork display when you don't have real artwork available.

## Setup

```bash
cd .scripts
python -m venv .venv          # canonical path: .scripts/.venv/ (see .gitignore)
source .venv/bin/activate.fish  # or .venv/bin/activate for bash
pip install -r requirements.txt  # one venv serves every .scripts/ helper
```

> The single canonical virtualenv for all `.scripts/` Python helpers lives at
> `.scripts/.venv/`. Use it for both this generator and the
> [seed-data generator](seed-data.md) rather than creating a second env.

## Usage

```bash
python subfolder_art_generator.py <media_dir> <output_dir> [options]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `media_dir` | Media directory containing subfolders to generate artwork for |
| `output_dir` | Output directory to save generated artwork |

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--size` | Image size as WIDTHxHEIGHT | `256x256` |
| `--overwrite` | Overwrite existing artwork files | disabled |
| `--dry-run` | Show what would be created without creating files | disabled |

### Examples

```bash
# Generate artwork for all subfolders in a media directory
python subfolder_art_generator.py ~/Media ~/Artwork

# Use custom image size
python subfolder_art_generator.py ~/Media ~/Artwork --size 512x512

# Preview without creating files
python subfolder_art_generator.py ~/Media ~/Artwork --dry-run

# Regenerate all artwork (overwrite existing)
python subfolder_art_generator.py ~/Media ~/Artwork --overwrite
```

## Output

The script creates PNG images in the output directory. Each image:

- Is named after the corresponding subfolder (e.g., `Photos.png` for a `Photos/` subfolder)
- Has a random background color
- Contains the folder name as centered text with automatic word wrapping
- Uses contrasting text color for readability

## Integration with Kartend

After generating artwork, configure your collection to use the output directory as the artwork directory:

```ini
[My Collection]
mediaDirectory=/path/to/media
artworkDirectory=/path/to/artwork
```

When browsing subcollections, Kartend will display the generated artwork for each subfolder.
